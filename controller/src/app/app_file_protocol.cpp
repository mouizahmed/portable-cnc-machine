#include "app/app_file_protocol.h"

#include <Arduino.h>
#include <string.h>

#include "app/app_file_service.h"
#include "app/app_job_service.h"
#include "protocol/protocol_defs.h"

extern "C" {
#include "grbl/vfs.h"
}

static uint8_t next_download_transfer_id = 1;
static uint8_t next_upload_transfer_id = 1;

static constexpr uint16_t UPLOAD_CHUNK_SIZE = 4096;
static constexpr uint16_t DOWNLOAD_CHUNK_SIZE = 4096;
static constexpr uint32_t DOWNLOAD_ACK_TIMEOUT_MS = 5000;

static struct {
    bool active = false;
    uint8_t transfer_id = PCNC_TRANSFER_ID_NONE;
    uint32_t request_seq = 0;
    uint32_t expected_size = 0;
    uint32_t bytes_written = 0;
    uint32_t expected_seq = 0;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t started_ms = 0;
    uint32_t write_total_ms = 0;
    uint32_t write_max_ms = 0;
    uint32_t write_count = 0;
    char name[PCNC_MAX_FILENAME_BYTES] = {};
    char path[PCNC_MAX_FILENAME_BYTES + 2] = {};
    vfs_file_t *file = nullptr;
} upload_session;

static void copy_fixed(char *dest, size_t dest_size, const char *src)
{
    if(dest == nullptr || dest_size == 0)
        return;

    memset(dest, 0, dest_size);

    if(src == nullptr)
        return;

    const size_t len = strnlen(src, dest_size - 1);
    memcpy(dest, src, len);
}

static void send_response(AppCdcTransport& transport, uint32_t request_seq,
                          const void *payload, uint16_t payload_len)
{
    transport.send_frame(FRAME_RESP, PCNC_TRANSFER_ID_NONE, request_seq,
                         (const uint8_t *)payload, payload_len);
}

static void send_event(AppCdcTransport& transport, const void *payload, uint16_t payload_len)
{
    transport.send_frame(FRAME_EVENT, PCNC_TRANSFER_ID_NONE, 0,
                         (const uint8_t *)payload, payload_len);
}

static void send_error(AppCdcTransport& transport, uint32_t request_seq,
                       ProtocolErrorCode error, const char *reason)
{
    RespError response = {};
    response.message_type = RESP_ERROR;
    response.request_seq = request_seq;
    response.error = (uint8_t)error;
    copy_fixed(response.reason, sizeof(response.reason), reason);
    send_response(transport, request_seq, &response, sizeof(response));
}

static void send_storage_error(AppCdcTransport& transport, uint32_t request_seq,
                               ProtocolErrorCode error,
                               ProtocolStorageOperation operation,
                               const char *detail)
{
    RespStorageError response = {};
    response.message_type = RESP_STORAGE_ERROR;
    response.request_seq = request_seq;
    response.error = (uint8_t)error;
    response.operation = (uint8_t)operation;
    copy_fixed(response.detail, sizeof(response.detail), detail);
    send_response(transport, request_seq, &response, sizeof(response));
}

static void write_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    static bool initialized = false;
    static uint32_t table[256];

    if(!initialized) {
        for(uint32_t value = 0; value < 256; value++) {
            uint32_t entry = value;
            for(uint8_t bit = 0; bit < 8; bit++)
                entry = (entry & 1u) != 0 ? (entry >> 1) ^ 0xEDB88320u : (entry >> 1);
            table[value] = entry;
        }
        initialized = true;
    }

    for(size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

    return crc;
}

static void close_upload_session(bool delete_partial)
{
    if(upload_session.file != nullptr) {
        vfs_close(upload_session.file);
        upload_session.file = nullptr;
    }

    if(delete_partial && upload_session.path[0] != '\0')
        vfs_unlink(upload_session.path);

    upload_session.active = false;
    upload_session.transfer_id = PCNC_TRANSFER_ID_NONE;
    upload_session.request_seq = 0;
    upload_session.expected_size = 0;
    upload_session.bytes_written = 0;
    upload_session.expected_seq = 0;
    upload_session.crc = 0xFFFFFFFFu;
    upload_session.started_ms = 0;
    upload_session.write_total_ms = 0;
    upload_session.write_max_ms = 0;
    upload_session.write_count = 0;
    upload_session.name[0] = '\0';
    upload_session.path[0] = '\0';
}

static void send_upload_ack(AppCdcTransport& transport)
{
    uint8_t payload[sizeof(uint32_t)] = {};
    write_u32_le(payload, upload_session.bytes_written);
    transport.send_frame(FRAME_UPLOAD_ACK,
                         upload_session.transfer_id,
                         upload_session.expected_seq == 0 ? 0 : upload_session.expected_seq - 1,
                         payload,
                         sizeof(payload));
}

static void send_file_upload_abort(AppCdcTransport& transport, uint32_t request_seq)
{
    RespFileUploadAbort response = {};
    response.message_type = RESP_FILE_UPLOAD_ABORT;
    response.request_seq = request_seq;
    send_response(transport, request_seq, &response, sizeof(response));
}

static void send_job_event(AppCdcTransport& transport)
{
    EventJob event = {};
    event.message_type = EVENT_JOB;
    event.has_job = app_job_service_has_job() ? 1 : 0;
    copy_fixed(event.name, sizeof(event.name), app_job_service_name());
    send_event(transport, &event, sizeof(event));
}

static bool reject_if_job_running(AppCdcTransport& transport,
                                  uint32_t request_seq,
                                  ProtocolStorageOperation operation)
{
    if(!app_job_service_is_running())
        return false;

    send_storage_error(transport, request_seq, ERROR_STORAGE_BUSY,
                       operation, "SD job is running");
    return true;
}

static bool read_fixed_string(char *dest, size_t dest_size, const char *src, size_t src_size)
{
    if(dest == nullptr || dest_size == 0 || src == nullptr || src_size == 0)
        return false;

    size_t i = 0;
    while(i < dest_size - 1 && i < src_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';

    return i > 0;
}

static bool build_sd_path(char *dest, size_t dest_size, const char *name)
{
    if(dest == nullptr || dest_size < 2 || name == nullptr || name[0] == '\0')
        return false;

    if(strchr(name, '/') != nullptr || strchr(name, '\\') != nullptr)
        return false;

    const size_t name_len = strlen(name);
    if(name_len + 2 > dest_size)
        return false;

    dest[0] = '/';
    memcpy(dest + 1, name, name_len + 1);
    return true;
}

static void begin_file_upload(AppCdcTransport& transport, uint32_t request_seq,
                              const CmdFileUpload *command)
{
    if(reject_if_job_running(transport, request_seq, STORAGE_OP_UPLOAD))
        return;

    char name[PCNC_MAX_FILENAME_BYTES] = {};
    char path[PCNC_MAX_FILENAME_BYTES + 2] = {};

    if(command == nullptr ||
       !read_fixed_string(name, sizeof(name), command->name, sizeof(command->name)) ||
       !build_sd_path(path, sizeof(path), name)) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_INVALID_FILENAME,
                           STORAGE_OP_UPLOAD, "Invalid filename");
        return;
    }

    if(upload_session.active) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_BUSY,
                           STORAGE_OP_UPLOAD, "Upload already active");
        return;
    }

    vfs_free_t *free_info = vfs_fgetfree("/");
    if(free_info == nullptr) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_NO_SD,
                           STORAGE_OP_UPLOAD, "SD not mounted");
        return;
    }

    if(free_info->size >= free_info->used &&
       free_info->size - free_info->used < command->size) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_NO_SPACE,
                           STORAGE_OP_UPLOAD, "No space");
        return;
    }

    vfs_stat_t stat = {};
    const bool exists = vfs_stat(path, &stat) == 0 && !stat.st_mode.directory;
    if(exists && command->overwrite == 0) {
        send_error(transport, request_seq, ERROR_UPLOAD_FILE_EXISTS, name);
        return;
    }

    vfs_file_t *file = vfs_open(path, "w");
    if(file == nullptr) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_WRITE_FAIL,
                           STORAGE_OP_UPLOAD, "Unable to open file");
        return;
    }

    upload_session.active = true;
    upload_session.transfer_id = next_upload_transfer_id++;
    if(next_upload_transfer_id == PCNC_TRANSFER_ID_NONE)
        next_upload_transfer_id++;
    upload_session.request_seq = request_seq;
    upload_session.expected_size = command->size;
    upload_session.bytes_written = 0;
    upload_session.expected_seq = 0;
    upload_session.crc = 0xFFFFFFFFu;
    upload_session.started_ms = millis();
    upload_session.write_total_ms = 0;
    upload_session.write_max_ms = 0;
    upload_session.write_count = 0;
    upload_session.file = file;
    copy_fixed(upload_session.name, sizeof(upload_session.name), name);
    copy_fixed(upload_session.path, sizeof(upload_session.path), path);

    RespFileUploadReady ready = {};
    ready.message_type = RESP_FILE_UPLOAD_READY;
    ready.request_seq = request_seq;
    ready.transfer_id = upload_session.transfer_id;
    ready.size = command->size;
    ready.chunk_size = UPLOAD_CHUNK_SIZE;
    copy_fixed(ready.name, sizeof(ready.name), name);
    send_response(transport, request_seq, &ready, sizeof(ready));
}

static void handle_upload_data(AppCdcTransport& transport, const AppCdcTransport::Frame& frame)
{
    if(!upload_session.active ||
       frame.transfer_id != upload_session.transfer_id ||
       upload_session.file == nullptr) {
        send_storage_error(transport, upload_session.request_seq, ERROR_STORAGE_INVALID_SESSION,
                           STORAGE_OP_UPLOAD, "Invalid upload session");
        return;
    }

    if(frame.seq < upload_session.expected_seq) {
        send_upload_ack(transport);
        return;
    }

    if(frame.seq != upload_session.expected_seq) {
        send_storage_error(transport, upload_session.request_seq, ERROR_STORAGE_BAD_SEQUENCE,
                           STORAGE_OP_UPLOAD, "Bad upload sequence");
        close_upload_session(true);
        return;
    }

    if(upload_session.bytes_written + frame.payload_len > upload_session.expected_size) {
        send_storage_error(transport, upload_session.request_seq, ERROR_STORAGE_SIZE_MISMATCH,
                           STORAGE_OP_UPLOAD, "Upload too large");
        close_upload_session(true);
        return;
    }

    const uint32_t write_started_ms = millis();
    const size_t written = vfs_write(frame.payload, 1, frame.payload_len, upload_session.file);
    const uint32_t write_ms = millis() - write_started_ms;
    upload_session.write_total_ms += write_ms;
    if(write_ms > upload_session.write_max_ms)
        upload_session.write_max_ms = write_ms;
    upload_session.write_count++;

    if(written != frame.payload_len) {
        send_storage_error(transport, upload_session.request_seq, ERROR_STORAGE_WRITE_FAIL,
                           STORAGE_OP_UPLOAD, "Write failed");
        close_upload_session(true);
        return;
    }

    upload_session.crc = crc32_update(upload_session.crc, frame.payload, frame.payload_len);
    upload_session.bytes_written += frame.payload_len;
    upload_session.expected_seq++;
    send_upload_ack(transport);
}

static void finish_file_upload(AppCdcTransport& transport, uint32_t request_seq,
                               const CmdFileUploadEnd *command)
{
    if(command == nullptr || !upload_session.active || upload_session.file == nullptr) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_INVALID_SESSION,
                           STORAGE_OP_UPLOAD, "Invalid upload session");
        return;
    }

    if(upload_session.bytes_written != upload_session.expected_size) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_SIZE_MISMATCH,
                           STORAGE_OP_UPLOAD, "Upload size mismatch");
        close_upload_session(true);
        return;
    }

    const uint32_t actual_crc = ~upload_session.crc;
    if(actual_crc != command->crc32) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_CRC_FAIL,
                           STORAGE_OP_UPLOAD, "Upload CRC mismatch");
        close_upload_session(true);
        return;
    }

    vfs_close(upload_session.file);
    upload_session.file = nullptr;

    RespFileUploadEnd done = {};
    done.message_type = RESP_FILE_UPLOAD_END;
    done.request_seq = request_seq;
    done.transfer_id = upload_session.transfer_id;
    done.size = upload_session.bytes_written;
    copy_fixed(done.name, sizeof(done.name), upload_session.name);
    send_response(transport, request_seq, &done, sizeof(done));

    close_upload_session(false);
    app_file_service_refresh(false);
}

static void send_file_list(AppCdcTransport& transport, uint32_t request_seq)
{
    app_file_service_refresh(false);
    const sd_gcode_file_list_t *files = app_file_service_files();

    if(files == nullptr || !files->sd_ready) {
        send_error(transport, request_seq, ERROR_STORAGE_NO_SD, "SD not mounted");
        return;
    }

    for(uint8_t i = 0; i < files->count; i++) {
        RespFileEntry entry = {};
        entry.message_type = RESP_FILE_ENTRY;
        entry.request_seq = request_seq;
        copy_fixed(entry.name, sizeof(entry.name), files->names[i]);
        entry.size = files->sizes[i];
        send_response(transport, request_seq, &entry, sizeof(entry));
    }

    RespFileListEnd end = {};
    end.message_type = RESP_FILE_LIST_END;
    end.request_seq = request_seq;
    end.count = files->count;

    vfs_free_t *free_info = vfs_fgetfree("/");
    if(free_info != nullptr && free_info->size >= free_info->used)
        end.free_bytes = free_info->size - free_info->used;
    else
        end.free_bytes = 0;

    send_response(transport, request_seq, &end, sizeof(end));
}

static void send_file_load(AppCdcTransport& transport, uint32_t request_seq,
                           const CmdFileLoad *command)
{
    if(reject_if_job_running(transport, request_seq, STORAGE_OP_LOAD))
        return;

    char name[PCNC_MAX_FILENAME_BYTES] = {};
    if(command == nullptr ||
       !read_fixed_string(name, sizeof(name), command->name, sizeof(command->name))) {
        send_storage_error(transport, request_seq, ERROR_MISSING_PARAM,
                           STORAGE_OP_LOAD, "Missing filename");
        return;
    }

    char reason[PCNC_MAX_REASON_BYTES] = {};
    if(!app_job_service_load(name, reason, sizeof(reason))) {
        send_storage_error(
            transport,
            request_seq,
            strcmp(reason, "File not found") == 0
                ? ERROR_STORAGE_FILE_NOT_FOUND
                : ERROR_STORAGE_INVALID_FILENAME,
            STORAGE_OP_LOAD,
            reason[0] == '\0' ? "Load failed" : reason);
        return;
    }

    RespFileLoad response = {};
    response.message_type = RESP_FILE_LOAD;
    response.request_seq = request_seq;
    copy_fixed(response.name, sizeof(response.name), app_job_service_name());
    send_response(transport, request_seq, &response, sizeof(response));
    send_job_event(transport);
}

static void send_file_unload(AppCdcTransport& transport, uint32_t request_seq)
{
    if(reject_if_job_running(transport, request_seq, STORAGE_OP_UNLOAD))
        return;

    app_job_service_unload();

    RespFileUnload response = {};
    response.message_type = RESP_FILE_UNLOAD;
    response.request_seq = request_seq;
    send_response(transport, request_seq, &response, sizeof(response));
    send_job_event(transport);
}

static bool wait_download_ack(AppCdcTransport& transport, uint8_t transfer_id, uint32_t seq)
{
    const uint32_t started = millis();
    AppCdcTransport::Frame frame;

    while(millis() - started < DOWNLOAD_ACK_TIMEOUT_MS) {
        while(transport.poll(frame)) {
            if(frame.type == FRAME_DOWNLOAD_ACK &&
               frame.transfer_id == transfer_id &&
               frame.seq == seq)
                return true;

            if(frame.type == FRAME_CMD && frame.payload_len > 0 &&
               frame.payload[0] == CMD_FILE_DOWNLOAD_ABORT)
                return false;
        }

        yield();
    }

    return false;
}

static void send_file_download(AppCdcTransport& transport, uint32_t request_seq,
                               const CmdFileDownload *command)
{
    if(reject_if_job_running(transport, request_seq, STORAGE_OP_DOWNLOAD))
        return;

    char name[PCNC_MAX_FILENAME_BYTES] = {};
    char path[PCNC_MAX_FILENAME_BYTES + 2] = {};

    if(command == nullptr ||
       !read_fixed_string(name, sizeof(name), command->name, sizeof(command->name)) ||
       !build_sd_path(path, sizeof(path), name)) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_INVALID_FILENAME,
                           STORAGE_OP_DOWNLOAD, "Invalid filename");
        return;
    }

    vfs_stat_t stat = {};
    if(vfs_stat(path, &stat) != 0 || stat.st_mode.directory) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_FILE_NOT_FOUND,
                           STORAGE_OP_DOWNLOAD, "File not found");
        return;
    }

    if(stat.st_size > UINT32_MAX) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_READ_FAIL,
                           STORAGE_OP_DOWNLOAD, "File too large");
        return;
    }

    vfs_file_t *file = vfs_open(path, "r");
    if(file == nullptr) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_READ_FAIL,
                           STORAGE_OP_DOWNLOAD, "Unable to open file");
        return;
    }

    const uint8_t transfer_id = next_download_transfer_id++;
    if(next_download_transfer_id == PCNC_TRANSFER_ID_NONE)
        next_download_transfer_id++;

    RespFileDownloadReady ready = {};
    ready.message_type = RESP_FILE_DOWNLOAD_READY;
    ready.request_seq = request_seq;
    ready.transfer_id = transfer_id;
    ready.size = (uint32_t)stat.st_size;
    ready.chunk_size = DOWNLOAD_CHUNK_SIZE;
    copy_fixed(ready.name, sizeof(ready.name), name);
    send_response(transport, request_seq, &ready, sizeof(ready));

    uint8_t buffer[DOWNLOAD_CHUNK_SIZE];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t seq = 0;
    bool ok = true;

    while(ok && !vfs_eof(file)) {
        const size_t bytes_read = vfs_read(buffer, 1, sizeof(buffer), file);
        if(bytes_read == 0)
            break;

        crc = crc32_update(crc, buffer, bytes_read);
        transport.send_frame(FRAME_DOWNLOAD_DATA, transfer_id, seq, buffer, (uint16_t)bytes_read);

        if(!wait_download_ack(transport, transfer_id, seq)) {
            ok = false;
            break;
        }

        seq++;
    }

    vfs_close(file);

    if(!ok) {
        RespFileDownloadAbort aborted = {};
        aborted.message_type = RESP_FILE_DOWNLOAD_ABORT;
        aborted.request_seq = request_seq;
        send_response(transport, request_seq, &aborted, sizeof(aborted));
        return;
    }

    RespFileDownloadEnd done = {};
    done.message_type = RESP_FILE_DOWNLOAD_END;
    done.request_seq = request_seq;
    done.transfer_id = transfer_id;
    done.crc32 = ~crc;
    copy_fixed(done.name, sizeof(done.name), name);
    send_response(transport, request_seq, &done, sizeof(done));
}

static void send_file_delete(AppCdcTransport& transport, uint32_t request_seq,
                             const CmdFileDelete *command)
{
    if(reject_if_job_running(transport, request_seq, STORAGE_OP_DELETE))
        return;

    char name[PCNC_MAX_FILENAME_BYTES] = {};
    char path[PCNC_MAX_FILENAME_BYTES + 2] = {};

    if(command == nullptr ||
       !read_fixed_string(name, sizeof(name), command->name, sizeof(command->name)) ||
       !build_sd_path(path, sizeof(path), name)) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_INVALID_FILENAME,
                           STORAGE_OP_DELETE, "Invalid filename");
        return;
    }

    vfs_stat_t stat = {};
    if(vfs_stat(path, &stat) != 0 || stat.st_mode.directory) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_FILE_NOT_FOUND,
                           STORAGE_OP_DELETE, "File not found");
        return;
    }

    if(vfs_unlink(path) != 0) {
        send_storage_error(transport, request_seq, ERROR_STORAGE_WRITE_FAIL,
                           STORAGE_OP_DELETE, "Delete failed");
        return;
    }

    app_file_service_refresh(false);

    RespFileDelete response = {};
    response.message_type = RESP_FILE_DELETE;
    response.request_seq = request_seq;
    copy_fixed(response.name, sizeof(response.name), name);
    send_response(transport, request_seq, &response, sizeof(response));

    if(strcmp(app_job_service_name(), name) == 0) {
        app_job_service_unload();
        send_job_event(transport);
    }
}

bool app_file_protocol_handle_command(AppCdcTransport& transport, const AppCdcTransport::Frame& frame)
{
    if(frame.type == FRAME_UPLOAD_DATA) {
        handle_upload_data(transport, frame);
        return true;
    }

    if(frame.type != FRAME_CMD)
        return false;

    if(frame.payload_len == 0)
        return false;

    switch(frame.payload[0]) {
        case CMD_FILE_LIST:
            send_file_list(transport, frame.seq);
            return true;

        case CMD_FILE_LOAD:
            if(frame.payload_len < sizeof(CmdFileLoad)) {
                send_storage_error(transport, frame.seq, ERROR_MISSING_PARAM,
                                   STORAGE_OP_LOAD, "Missing filename");
                return true;
            }
            send_file_load(transport, frame.seq, (const CmdFileLoad *)frame.payload);
            return true;

        case CMD_FILE_UNLOAD:
            send_file_unload(transport, frame.seq);
            return true;

        case CMD_FILE_DELETE:
            if(frame.payload_len < sizeof(CmdFileDelete)) {
                send_storage_error(transport, frame.seq, ERROR_MISSING_PARAM,
                                   STORAGE_OP_DELETE, "Missing filename");
                return true;
            }
            send_file_delete(transport, frame.seq, (const CmdFileDelete *)frame.payload);
            return true;

        case CMD_FILE_UPLOAD:
            if(frame.payload_len < sizeof(CmdFileUpload)) {
                send_storage_error(transport, frame.seq, ERROR_UPLOAD_MISSING_PARAM,
                                   STORAGE_OP_UPLOAD, "Missing upload metadata");
                return true;
            }
            begin_file_upload(transport, frame.seq, (const CmdFileUpload *)frame.payload);
            return true;

        case CMD_FILE_UPLOAD_END:
            if(frame.payload_len < sizeof(CmdFileUploadEnd)) {
                send_storage_error(transport, frame.seq, ERROR_UPLOAD_MISSING_PARAM,
                                   STORAGE_OP_UPLOAD, "Missing upload CRC");
                return true;
            }
            finish_file_upload(transport, frame.seq, (const CmdFileUploadEnd *)frame.payload);
            return true;

        case CMD_FILE_UPLOAD_ABORT:
            if(upload_session.active)
                close_upload_session(true);
            send_file_upload_abort(transport, frame.seq);
            return true;

        case CMD_FILE_DOWNLOAD:
            if(frame.payload_len < sizeof(CmdFileDownload)) {
                send_storage_error(transport, frame.seq, ERROR_DOWNLOAD_MISSING_PARAM,
                                   STORAGE_OP_DOWNLOAD, "Missing filename");
                return true;
            }
            send_file_download(transport, frame.seq, (const CmdFileDownload *)frame.payload);
            return true;

        case CMD_FILE_DOWNLOAD_ABORT: {
            RespFileDownloadAbort response = {};
            response.message_type = RESP_FILE_DOWNLOAD_ABORT;
            response.request_seq = frame.seq;
            send_response(transport, frame.seq, &response, sizeof(response));
            return true;
        }

        default:
            return false;
    }
}
