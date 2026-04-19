#include "protocol/desktop_protocol.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

#include "pico/stdlib.h"

static void percent_decode_in_place(char* text);
static void percent_encode_value(const char* input, char* output, size_t output_size);

// -- Constructor --------------------------------------------------------------

DesktopProtocol::DesktopProtocol(UsbCdcTransport& transport,
                                 MachineFsm& msm,
                                 JogStateMachine& jogs,
                                 JobStateMachine& jobs,
                                 LoadedJobStorage& loaded_job_storage,
                                 StorageService& storage,
                                 SdSpiCard& sd)
    : transport_(transport),
      msm_(msm),
      jogs_(jogs),
      jobs_(jobs),
      loaded_job_storage_(loaded_job_storage),
      storage_(storage),
      sd_(sd) {}

// -- Poll / dispatch ----------------------------------------------------------

void DesktopProtocol::poll() {
    UsbCdcTransport::FramePacket frame{};
    while (true) {
        const auto kind = transport_.poll(line_, sizeof(line_), frame);
        if (kind == UsbCdcTransport::PacketKind::None) {
            tick_transfer_retries();
            return;
        }
        if (kind == UsbCdcTransport::PacketKind::Line) {
            dispatch(line_);
        } else if (kind == UsbCdcTransport::PacketKind::Frame) {
            dispatch_frame(frame);
        }
    }
}

void DesktopProtocol::dispatch(const char* line) {
    if (line[0] != '@') return;

    // Find first space to split verb from params
    const char* space = std::strchr(line, ' ');
    char verb[32];
    const char* params = "";

    if (space) {
        size_t verb_len = static_cast<size_t>(space - line);
        if (verb_len >= sizeof(verb)) verb_len = sizeof(verb) - 1;
        std::memcpy(verb, line, verb_len);
        verb[verb_len] = '\0';
        params = space + 1;
    } else {
        std::strncpy(verb, line, sizeof(verb) - 1);
        verb[sizeof(verb) - 1] = '\0';
    }

    if (std::strcmp(verb, "@PING")             == 0) { handle_ping(); }
    else if (std::strcmp(verb, "@INFO")        == 0) { handle_info(); }
    else if (std::strcmp(verb, "@STATUS")      == 0) { handle_status(); }
    else if (std::strcmp(verb, "@HOME")        == 0) { handle_home(); }
    else if (std::strcmp(verb, "@JOG")         == 0) { handle_jog(params); }
    else if (std::strcmp(verb, "@JOG_CANCEL")  == 0) { handle_jog_cancel(); }
    else if (std::strcmp(verb, "@ZERO")        == 0) { handle_zero(params); }
    else if (std::strcmp(verb, "@START")       == 0) { handle_start(); }
    else if (std::strcmp(verb, "@PAUSE")       == 0) { handle_pause(); }
    else if (std::strcmp(verb, "@RESUME")      == 0) { handle_resume(); }
    else if (std::strcmp(verb, "@ABORT")       == 0) { handle_abort(); }
    else if (std::strcmp(verb, "@ESTOP")       == 0) { handle_estop(); }
    else if (std::strcmp(verb, "@RESET")       == 0) { handle_reset(); }
    else if (std::strcmp(verb, "@SPINDLE_ON")  == 0) { handle_spindle_on(params); }
    else if (std::strcmp(verb, "@SPINDLE_OFF") == 0) { handle_spindle_off(); }
    else if (std::strcmp(verb, "@OVERRIDE")    == 0) { handle_override(params); }
    else if (std::strcmp(verb, "@FILE_LIST")   == 0) { handle_file_list(); }
    else if (std::strcmp(verb, "@FILE_LOAD")   == 0) { handle_file_load(params); }
    else if (std::strcmp(verb, "@FILE_UNLOAD") == 0) { handle_file_unload(); }
    else if (std::strcmp(verb, "@FILE_DELETE")   == 0) { handle_file_delete(params); }
    else if (std::strcmp(verb, "@FILE_UPLOAD") == 0) { handle_file_upload(params); }
    else if (std::strcmp(verb, "@FILE_UPLOAD_END")   == 0) { handle_file_upload_end(params); }
    else if (std::strcmp(verb, "@FILE_UPLOAD_ABORT") == 0) { handle_file_upload_abort(); }
    else if (std::strcmp(verb, "@FILE_DOWNLOAD") == 0) { handle_file_download(params); }
    else if (std::strcmp(verb, "@FILE_DOWNLOAD_ACK") == 0) { handle_file_download_ack(params); }
    else if (std::strcmp(verb, "@FILE_DOWNLOAD_ABORT") == 0) { handle_file_download_abort(); }
    // Unknown verbs are silently ignored
}

void DesktopProtocol::dispatch_frame(const UsbCdcTransport::FramePacket& frame) {
    if (frame.type == kUploadDataFrameType) {
        handle_upload_chunk_data(frame.transfer_id, frame.seq, frame.payload, frame.payload_len);
        return;
    }

    if (frame.type == kDownloadAckFrameType) {
        if (!download_.active) {
            if (download_.completion_valid &&
                frame.transfer_id == download_.completion_transfer_id &&
                frame.seq == download_.completion_last_seq) {
                send_download_complete();
            }
            return;
        }
        if (frame.transfer_id != download_.transfer_id) {
            emit_error("DOWNLOAD_INVALID_SESSION");
            return;
        }
        if (!download_.awaiting_ack) {
            return;
        }
        if (frame.seq < download_.last_chunk_seq) {
            return;
        }
        if (frame.seq != download_.last_chunk_seq) {
            transport_.send_fmt("@ERROR DOWNLOAD_BAD_SEQUENCE SEQ=%lu EXPECTED=%lu",
                                static_cast<unsigned long>(frame.seq),
                                static_cast<unsigned long>(download_.last_chunk_seq));
            return;
        }
        download_.awaiting_ack = false;
        send_next_download_chunk();
    }
}

// -- Outbound helpers ---------------------------------------------------------

void DesktopProtocol::emit_state() {
    const char* s = "UNKNOWN";
    switch (msm_.state()) {
        case MachineOperationState::Booting:            s = "BOOTING";             break;
        case MachineOperationState::Syncing:            s = "SYNCING";             break;
        case MachineOperationState::TeensyDisconnected: s = "TEENSY_DISCONNECTED"; break;
        case MachineOperationState::Idle:               s = "IDLE";                break;
        case MachineOperationState::Homing:             s = "HOMING";              break;
        case MachineOperationState::Jog:                s = "JOG";                 break;
        case MachineOperationState::Starting:           s = "STARTING";            break;
        case MachineOperationState::Running:            s = "RUNNING";             break;
        case MachineOperationState::Hold:               s = "HOLD";                break;
        case MachineOperationState::Fault:              s = "FAULT";               break;
        case MachineOperationState::Estop:              s = "ESTOP";               break;
        case MachineOperationState::CommsFault:         s = "COMMS_FAULT";         break;
        case MachineOperationState::Uploading:          s = "UPLOADING";           break;
    }
    transport_.send_fmt("@STATE %s", s);
}

void DesktopProtocol::emit_caps() {
    const CapsFlags c = msm_.caps();
    transport_.send_fmt(
        "@CAPS MOTION=%d PROBE=%d SPINDLE=%d FILE_LOAD=%d "
        "JOB_START=%d JOB_PAUSE=%d JOB_RESUME=%d JOB_ABORT=%d OVERRIDES=%d RESET=%d",
        c.motion      ? 1 : 0,
        c.probe       ? 1 : 0,
        c.spindle     ? 1 : 0,
        c.file_load ? 1 : 0,
        c.job_start   ? 1 : 0,
        c.job_pause   ? 1 : 0,
        c.job_resume  ? 1 : 0,
        c.job_abort   ? 1 : 0,
        c.overrides   ? 1 : 0,
        c.reset       ? 1 : 0);
}

void DesktopProtocol::emit_safety() {
    const char* s = "SAFE";
    switch (msm_.safety()) {
        case SafetyLevel::Safe:       s = "SAFE";       break;
        case SafetyLevel::Monitoring: s = "MONITORING"; break;
        case SafetyLevel::Warning:    s = "WARNING";    break;
        case SafetyLevel::Critical:   s = "CRITICAL";   break;
    }
    transport_.send_fmt("@SAFETY %s", s);
}

void DesktopProtocol::emit_state_update(bool mark_changed) {
    emit_state();
    emit_caps();
    emit_safety();
    if (mark_changed) {
        state_changed_ = true;
    }
}

void DesktopProtocol::emit_job() {
    if (const FileEntry* loaded = jobs_.loaded_entry()) {
        char encoded_name[192]{};
        percent_encode_value(loaded->name, encoded_name, sizeof(encoded_name));
        transport_.send_fmt("@JOB NAME=%s", encoded_name);
        return;
    }

    transport_.send_fmt("@JOB NAME=NONE");
}

void DesktopProtocol::emit_position() {
    const double x = static_cast<double>(jogs_.x());
    const double y = static_cast<double>(jogs_.y());
    const double z = static_cast<double>(jogs_.z());
    transport_.send_fmt("@POS MX=%.3f MY=%.3f MZ=%.3f WX=%.3f WY=%.3f WZ=%.3f",
                        x, y, z, x, y, z);
}

void DesktopProtocol::emit_event(const char* name) {
    transport_.send_fmt("@EVENT %s", name);
}

void DesktopProtocol::emit_event_kv(const char* name, const char* kv) {
    if (kv && kv[0]) {
        transport_.send_fmt("@EVENT %s %s", name, kv);
    } else {
        transport_.send_fmt("@EVENT %s", name);
    }
}

void DesktopProtocol::emit_ok(const char* token) {
    transport_.send_fmt("@OK %s", token);
}

void DesktopProtocol::emit_ok_kv(const char* token, const char* kv) {
    if (kv && kv[0]) {
        transport_.send_fmt("@OK %s %s", token, kv);
    } else {
        transport_.send_fmt("@OK %s", token);
    }
}

void DesktopProtocol::emit_error(const char* reason) {
    transport_.send_fmt("@ERROR %s", reason);
}

void DesktopProtocol::reset_upload_completion() {
    upload_.completion_valid = false;
    upload_.completion_name[0] = '\0';
    upload_.completion_size = 0;
    upload_.completion_crc = 0;
    upload_.completion_transfer_id = 0;
}

void DesktopProtocol::reset_download_completion() {
    download_.completion_valid = false;
    download_.completion_name[0] = '\0';
    download_.completion_crc = 0;
    download_.completion_last_seq = 0;
    download_.completion_transfer_id = 0;
}

void DesktopProtocol::send_upload_ready() {
    char encoded_name[192]{};
    percent_encode_value(upload_.name, encoded_name, sizeof(encoded_name));
    char kv[256];
    std::snprintf(kv, sizeof(kv), "NAME=%s SIZE=%lu ID=%u CHUNK=%u",
                  encoded_name,
                  static_cast<unsigned long>(upload_.expected_size),
                  static_cast<unsigned int>(upload_.transfer_id),
                  static_cast<unsigned int>(kTransferRawChunkSize));
    emit_ok_kv("FILE_UPLOAD_READY", kv);
}

void DesktopProtocol::send_upload_chunk_ack(uint32_t sequence, uint32_t bytes_committed) {
    uint8_t ack_payload[sizeof(uint32_t)]{};
    ack_payload[0] = static_cast<uint8_t>(bytes_committed & 0xFFu);
    ack_payload[1] = static_cast<uint8_t>((bytes_committed >> 8) & 0xFFu);
    ack_payload[2] = static_cast<uint8_t>((bytes_committed >> 16) & 0xFFu);
    ack_payload[3] = static_cast<uint8_t>((bytes_committed >> 24) & 0xFFu);
    transport_.send_frame(kUploadAckFrameType, upload_.transfer_id, sequence, ack_payload, sizeof(ack_payload));
}

void DesktopProtocol::send_upload_complete() {
    char encoded_name[192]{};
    percent_encode_value(upload_.completion_name, encoded_name, sizeof(encoded_name));
    transport_.send_fmt("@OK FILE_UPLOAD_END NAME=%s SIZE=%lu ID=%u",
                        encoded_name,
                        static_cast<unsigned long>(upload_.completion_size),
                        static_cast<unsigned int>(upload_.completion_transfer_id));
}

void DesktopProtocol::send_download_ready() {
    char encoded_name[192]{};
    percent_encode_value(download_.name, encoded_name, sizeof(encoded_name));
    char kv[256];
    std::snprintf(kv, sizeof(kv), "NAME=%s SIZE=%lu ID=%u CHUNK=%u",
                  encoded_name,
                  static_cast<unsigned long>(download_.total_size),
                  static_cast<unsigned int>(download_.transfer_id),
                  static_cast<unsigned int>(kTransferRawChunkSize));
    emit_ok_kv("FILE_DOWNLOAD_READY", kv);
}

void DesktopProtocol::send_download_complete() {
    char encoded_name[192]{};
    percent_encode_value(download_.completion_name, encoded_name, sizeof(encoded_name));
    transport_.send_fmt("@OK FILE_DOWNLOAD_END NAME=%s CRC=%08lx ID=%u",
                        encoded_name,
                        static_cast<unsigned long>(download_.completion_crc),
                        static_cast<unsigned int>(download_.completion_transfer_id));
}

void DesktopProtocol::resend_download_chunk() {
    if (!download_.active || !download_.awaiting_ack || download_.last_chunk_len == 0) {
        return;
    }

    transport_.send_frame(kDownloadDataFrameType,
                          download_.transfer_id,
                          download_.last_chunk_seq,
                          chunk_raw_,
                          download_.last_chunk_len);
    download_.last_send_ms = to_ms_since_boot(get_absolute_time());
}

void DesktopProtocol::tick_transfer_retries() {
    if (!download_.active || !download_.awaiting_ack) {
        return;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - download_.last_send_ms) >= kDownloadResendIntervalMs) {
        resend_download_chunk();
    }
}

bool DesktopProtocol::load_job_by_index(int16_t index) {
    if (!try_load_job_by_index(index)) {
        return false;
    }

    if (const FileEntry* loaded = jobs_.loaded_entry()) {
        loaded_job_storage_.save(loaded->name);
    }
    emit_state_update();
    emit_job();
    return true;
}

bool DesktopProtocol::load_job_by_name(const char* name) {
    const int16_t index = find_job_index_by_name(name);
    if (index < 0) {
        return false;
    }

    return load_job_by_index(index);
}

bool DesktopProtocol::unload_job() {
    if (!try_unload_job()) {
        return false;
    }

    loaded_job_storage_.clear();
    emit_state_update();
    emit_job();
    return true;
}

bool DesktopProtocol::restore_persisted_job() {
    char name[sizeof(FileEntry{}.name)]{};
    if (!loaded_job_storage_.load(name, sizeof(name))) {
        return false;
    }

    if (!load_job_by_name(name)) {
        loaded_job_storage_.clear();
        return false;
    }

    return true;
}

// -- Inject helper ------------------------------------------------------------

void DesktopProtocol::inject(MachineEvent ev) {
    msm_.handle_event(ev);
}

// -- Command handlers ---------------------------------------------------------

void DesktopProtocol::handle_ping() {
    emit_ok("PONG");
}

void DesktopProtocol::handle_info() {
    transport_.send_fmt("@INFO FIRMWARE=0.1.0 BOARD=PICO2W TEENSY=%s",
                        msm_.teensy_connected() ? "CONNECTED" : "DISCONNECTED");
    // Push current state + caps immediately after @INFO so the desktop always has
    // an up-to-date view on connect (the @STATE emitted at startup is lost if the
    // desktop wasn't connected yet).
    emit_state_update(false);
    emit_job();
}

void DesktopProtocol::handle_status() {
    emit_state_update(false);
    emit_position();
    emit_job();
}

void DesktopProtocol::handle_home() {
    CapsFlags c = msm_.caps();
    if (!c.motion) {
        emit_error("INVALID_STATE");
        return;
    }
    inject(MachineEvent::HomeCmd);
    emit_state_update();
    emit_ok("HOME");

    // Stub: immediately simulate homing completion (GrblIdle while in HOMING
    // sets all_axes_homed_ = true and transitions to IDLE)
    inject(MachineEvent::GrblIdle);
    emit_state_update();
}

void DesktopProtocol::handle_jog(const char* params) {
    CapsFlags c = msm_.caps();
    if (!c.motion) {
        emit_error("INVALID_STATE");
        return;
    }
    // params contains AXIS=X DIST=10.0 FEED=500 etc. -- passed through for future use
    (void)params;
    inject(MachineEvent::JogCmd);
    emit_ok_kv("JOG", "");
    emit_state_update();

    // Stub: immediately complete the jog
    inject(MachineEvent::GrblIdle);
    emit_state_update();
}

void DesktopProtocol::handle_jog_cancel() {
    inject(MachineEvent::JogStop);
    emit_ok("JOG_CANCEL");

    // Stub: immediately return to IDLE
    inject(MachineEvent::GrblIdle);
    emit_state_update();
}

void DesktopProtocol::handle_zero(const char* params) {
    (void)params;
    emit_ok("ZERO");
}

void DesktopProtocol::handle_start() {
    CapsFlags c = msm_.caps();
    if (!c.job_start) {
        emit_error("INVALID_STATE");
        return;
    }
    inject(MachineEvent::StartCmd);
    emit_ok("START");
    emit_state_update();

    // Stub: simulate the full job cycle
    inject(MachineEvent::GrblCycle);
    emit_state_update();

    inject(MachineEvent::JobStreamComplete);
    inject(MachineEvent::GrblIdle);
    emit_event("JOB_COMPLETE");
    emit_state_update();
}

void DesktopProtocol::handle_pause() {
    if (!msm_.caps().job_pause) {
        emit_error("INVALID_STATE");
        return;
    }
    inject(MachineEvent::PauseCmd);
    emit_ok("PAUSE");

    // Stub: simulate hold sequence
    inject(MachineEvent::GrblHoldPending);
    inject(MachineEvent::GrblHoldComplete);
    emit_state_update();
}

void DesktopProtocol::handle_resume() {
    if (!msm_.caps().job_resume) {
        emit_error("INVALID_STATE");
        return;
    }
    inject(MachineEvent::ResumeCmd);
    emit_ok("RESUME");

    // Stub: simulate cycle resume
    inject(MachineEvent::GrblCycle);
    emit_state_update();
}

void DesktopProtocol::handle_abort() {
    CapsFlags c = msm_.caps();
    if (!c.job_abort) {
        emit_error("INVALID_STATE");
        return;
    }
    inject(MachineEvent::AbortCmd);
    emit_ok("ABORT");

    // Stub: simulate grbl acknowledging reset
    inject(MachineEvent::GrblIdle);
    emit_state_update();
}

void DesktopProtocol::handle_estop() {
    // Simulate E-stop assertion in stub mode
    inject(MachineEvent::HwEstopAsserted);
    emit_ok("ESTOP");
    emit_state_update();
}

void DesktopProtocol::handle_reset() {
    if (!msm_.caps().reset) {
        emit_error("INVALID_STATE");
        return;
    }
    inject(MachineEvent::ResetCmd);
    emit_ok("RESET");

    // Stub: simulate grbl returning to idle after reset
    inject(MachineEvent::GrblIdle);
    emit_state_update();
}

void DesktopProtocol::handle_spindle_on(const char* params) {
    (void)params;
    emit_ok("SPINDLE_ON");
}

void DesktopProtocol::handle_spindle_off() {
    emit_ok("SPINDLE_OFF");
}

void DesktopProtocol::handle_override(const char* params) {
    (void)params;
    emit_ok("OVERRIDE");
}

void DesktopProtocol::handle_file_list() {
    if (transfer_active()) {
        emit_error("TRANSFER_BUSY");
        return;
    }
    if (!msm_.sd_mounted()) {
        emit_error("SD_NOT_MOUNTED");
        return;
    }

    DIR dir;
    FILINFO info;
    FRESULT fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        emit_error("SD_READ_FAIL");
        return;
    }

    int count = 0;
    while (true) {
        fr = f_readdir(&dir, &info);
        if (fr != FR_OK || info.fname[0] == '\0') break;
        if (info.fattrib & AM_DIR) continue;

        // Check for supported G-code extensions (FAT32 short names are uppercase)
        const char* dot = std::strrchr(info.fname, '.');
        if (!dot) continue;
        if (std::strcmp(dot, ".gcode") != 0 && std::strcmp(dot, ".GCODE") != 0 &&
            std::strcmp(dot, ".nc")    != 0 && std::strcmp(dot, ".NC")    != 0 &&
            std::strcmp(dot, ".ngc")   != 0 && std::strcmp(dot, ".NGC")   != 0 &&
            std::strcmp(dot, ".tap")   != 0 && std::strcmp(dot, ".TAP")   != 0 &&
            std::strcmp(dot, ".gc")    != 0 && std::strcmp(dot, ".GC")    != 0) continue;

        char encoded_name[192]{};
        percent_encode_value(info.fname, encoded_name, sizeof(encoded_name));
        transport_.send_fmt("@FILE NAME=%s SIZE=%lu", encoded_name, (unsigned long)info.fsize);
        count++;
    }
    f_closedir(&dir);

    // Read free space
    FATFS* fs;
    DWORD free_clust = 0;
    uint64_t free_bytes = 0;
    if (f_getfree("0:", &free_clust, &fs) == FR_OK) {
        free_bytes = (uint64_t)free_clust * fs->csize * 512u;
    }

    transport_.send_fmt("@OK FILE_LIST_END COUNT=%d FREE=%llu",
                        count, (unsigned long long)free_bytes);
}

static bool is_sd_capable_state(MachineOperationState s) {
    return s == MachineOperationState::Idle ||
           s == MachineOperationState::TeensyDisconnected;
}

static bool is_safe_storage_name(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    for (const char* p = name; *p != '\0'; ++p) {
        const char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c < 0x20) {
            return false;
        }
    }

    return std::strlen(name) < 64;
}

static int from_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void percent_decode_in_place(char* text) {
    if (text == nullptr) {
        return;
    }

    char* write = text;
    for (char* read = text; *read != '\0'; ++read) {
        if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            const int hi = from_hex(read[1]);
            const int lo = from_hex(read[2]);
            if (hi >= 0 && lo >= 0) {
                *write++ = static_cast<char>((hi << 4) | lo);
                read += 2;
                continue;
            }
        }

        *write++ = *read;
    }
    *write = '\0';
}

static bool should_escape_value_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) {
        return false;
    }
    return c != '-' && c != '_' && c != '.' && c != '~';
}

static void percent_encode_value(const char* input, char* output, size_t output_size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (output == nullptr || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == nullptr) {
        return;
    }

    size_t write = 0;
    for (const char* read = input; *read != '\0' && write + 1 < output_size; ++read) {
        const unsigned char c = static_cast<unsigned char>(*read);
        if (should_escape_value_char(static_cast<char>(c))) {
            if (write + 3 >= output_size) {
                break;
            }
            output[write++] = '%';
            output[write++] = kHex[(c >> 4) & 0x0F];
            output[write++] = kHex[c & 0x0F];
            continue;
        }
        output[write++] = static_cast<char>(c);
    }
    output[write] = '\0';
}

int16_t DesktopProtocol::find_job_index_by_name(const char* name) const {
    if (name == nullptr || name[0] == '\0') {
        return -1;
    }

    for (std::size_t i = 0; i < jobs_.count(); ++i) {
        if (std::strcmp(jobs_.entry(i).name, name) == 0) {
            return static_cast<int16_t>(i);
        }
    }

    return -1;
}

bool DesktopProtocol::try_load_job_by_index(int16_t index) {
    if (!is_sd_capable_state(msm_.state()) || !msm_.sd_mounted()) {
        return false;
    }

    if (index < 0 || index >= static_cast<int16_t>(jobs_.count())) {
        return false;
    }

    if (jobs_.loaded_index() != index &&
        !jobs_.handle_event(JobEvent::LoadFile, index)) {
        return false;
    }

    inject(MachineEvent::JobLoaded);
    return true;
}

bool DesktopProtocol::try_unload_job() {
    if (!is_sd_capable_state(msm_.state()) || !jobs_.has_loaded_job()) {
        return false;
    }

    if (!jobs_.handle_event(JobEvent::ClearLoadedFile)) {
        return false;
    }

    inject(MachineEvent::JobUnloaded);
    return true;
}

void DesktopProtocol::handle_file_load(const char* params) {
    if (transfer_active()) {
        emit_error("TRANSFER_BUSY");
        return;
    }
    if (!is_sd_capable_state(msm_.state())) {
        emit_error("INVALID_STATE");
        return;
    }
    if (!msm_.sd_mounted()) {
        emit_error("SD_NOT_MOUNTED");
        return;
    }

    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("MISSING_PARAM");
        return;
    }
    if (!is_safe_storage_name(name)) {
        emit_error("INVALID_FILENAME");
        return;
    }

    const int16_t index = find_job_index_by_name(name);
    if (index < 0 || !load_job_by_index(index)) {
        emit_error("FILE_NOT_FOUND");
        return;
    }

    char encoded_name[192]{};
    percent_encode_value(name, encoded_name, sizeof(encoded_name));
    char kv[220];
    std::snprintf(kv, sizeof(kv), "NAME=%s", encoded_name);
    emit_ok_kv("FILE_LOAD", kv);
}

void DesktopProtocol::handle_file_unload() {
    if (transfer_active()) {
        emit_error("TRANSFER_BUSY");
        return;
    }
    if (!is_sd_capable_state(msm_.state())) {
        emit_error("INVALID_STATE");
        return;
    }

    if (!unload_job()) {
        emit_error("NO_JOB_LOADED");
        return;
    }

    emit_ok("FILE_UNLOAD");
}

void DesktopProtocol::handle_file_delete(const char* params) {
    if (transfer_active()) {
        emit_error("TRANSFER_BUSY");
        return;
    }
    if (!msm_.sd_mounted()) {
        emit_error("SD_NOT_MOUNTED");
        return;
    }

    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("MISSING_PARAM");
        return;
    }
    if (!is_safe_storage_name(name)) {
        emit_error("INVALID_FILENAME");
        return;
    }

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    FRESULT r = f_unlink(path);
    if (r == FR_OK) {
        const FileEntry* loaded = jobs_.loaded_entry();
        const bool deleted_loaded_job =
            loaded != nullptr && std::strcmp(loaded->name, name) == 0;
        if (deleted_loaded_job) {
            try_unload_job();
            loaded_job_storage_.clear();
        }

        file_list_changed_ = true;
        char encoded_name[192]{};
        percent_encode_value(name, encoded_name, sizeof(encoded_name));
        char kv[220];
        std::snprintf(kv, sizeof(kv), "NAME=%s", encoded_name);
        emit_ok_kv("FILE_DELETE", kv);
        if (deleted_loaded_job) {
            emit_state_update();
            emit_job();
        }
    } else {
        emit_error("FILE_NOT_FOUND");
    }
}

void DesktopProtocol::handle_file_upload(const char* params) {
    if (transfer_active()) {
        char name[64] = {};
        const uint32_t size = param_get_u32(params, "SIZE", 0);
        if (upload_.active &&
            param_get(params, "NAME", name, sizeof(name)) &&
            std::strcmp(name, upload_.name) == 0 &&
            size == upload_.expected_size) {
            send_upload_ready();
            return;
        }
        emit_error("UPLOAD_BUSY");
        return;
    }
    if (!is_sd_capable_state(msm_.state())) {
        emit_error("UPLOAD_INVALID_STATE");
        return;
    }
    if (!msm_.sd_mounted()) {
        emit_error("UPLOAD_SD_NOT_MOUNTED");
        return;
    }

    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("UPLOAD_MISSING_PARAM");
        return;
    }
    if (!is_safe_storage_name(name)) {
        emit_error("UPLOAD_INVALID_FILENAME");
        return;
    }

    uint32_t size      = param_get_u32(params, "SIZE", 0);
    int      overwrite = param_get_int(params, "OVERWRITE", 0);

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    // Check if file already exists
    FILINFO info;
    FRESULT stat_r = f_stat(path, &info);
    if (stat_r == FR_OK && !overwrite) {
        char encoded_name[192]{};
        percent_encode_value(name, encoded_name, sizeof(encoded_name));
        transport_.send_fmt("@ERROR UPLOAD_FILE_EXISTS NAME=%s", encoded_name);
        return;
    }
    if (stat_r == FR_OK && overwrite) {
        f_unlink(path);
    }

    // Check free space
    FATFS* fs;
    DWORD free_clust = 0;
    uint64_t free_bytes = 0;
    if (f_getfree("0:", &free_clust, &fs) == FR_OK) {
        free_bytes = (uint64_t)free_clust * fs->csize * 512u;
    }
    if (size > 0 && (uint64_t)size > free_bytes) {
        transport_.send_fmt("@ERROR UPLOAD_SD_FULL FREE=%llu", (unsigned long long)free_bytes);
        return;
    }

    // Zero the FIL struct before (re)use -- f_open() expects a fresh object;
    // stale fields from a previous close can cause undefined behaviour in FatFS.
    upload_.file = FIL{};

    // Open file for writing
    FRESULT fr = f_open(&upload_.file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        emit_error("UPLOAD_SD_WRITE_FAIL");
        return;
    }

    // Initialise upload state
    reset_upload_completion();
    upload_.active        = true;
    upload_.expected_size = size;
    upload_.bytes_written = 0;
    upload_.expected_seq  = 0;
    upload_.crc_running   = 0xFFFFFFFFu;
    upload_.transfer_id   = next_transfer_id_++;
    upload_.last_ack_valid = false;
    upload_.last_ack_seq = 0;
    upload_.last_ack_bytes = 0;
    std::strncpy(upload_.name, name, sizeof(upload_.name) - 1);
    upload_.name[sizeof(upload_.name) - 1] = '\0';

    state_changed_ = true;
    send_upload_ready();
}

void DesktopProtocol::handle_file_upload_end(const char* params) {
    if (!upload_.active) {
        char crc_str[16] = {};
        if (param_get(params, "CRC", crc_str, sizeof(crc_str))) {
            const uint32_t expected_crc = static_cast<uint32_t>(std::strtoul(crc_str, nullptr, 16));
            if (upload_.completion_valid && expected_crc == upload_.completion_crc) {
                send_upload_complete();
                return;
            }
        }
        emit_error("UPLOAD_NO_SESSION");
        return;
    }

    char crc_str[16] = {};
    param_get(params, "CRC", crc_str, sizeof(crc_str));

    // Close the file
    f_close(&upload_.file);

    if (upload_.expected_size > 0 && upload_.bytes_written != upload_.expected_size) {
        char path[80];
        std::snprintf(path, sizeof(path), "0:/%s", upload_.name);
        f_unlink(path);
        upload_.active = false;
        state_changed_ = true;
        emit_error("UPLOAD_SIZE_MISMATCH");
        return;
    }

    uint32_t final_crc = ~upload_.crc_running;
    uint32_t expected_crc = (uint32_t)std::strtoul(crc_str, nullptr, 16);

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", upload_.name);

    if (final_crc != expected_crc) {
        f_unlink(path);
        upload_.active = false;
        state_changed_ = true;
        emit_error("UPLOAD_CRC_FAIL");
        return;
    }

    char name_copy[64];
    uint32_t bytes_copy = upload_.bytes_written;
    std::strncpy(name_copy, upload_.name, sizeof(name_copy));
    name_copy[sizeof(name_copy) - 1] = '\0';

    upload_.active = false;
    upload_.completion_valid = true;
    upload_.completion_size = bytes_copy;
    upload_.completion_crc = final_crc;
    upload_.completion_transfer_id = upload_.transfer_id;
    std::strncpy(upload_.completion_name, name_copy, sizeof(upload_.completion_name) - 1);
    upload_.completion_name[sizeof(upload_.completion_name) - 1] = '\0';
    file_list_changed_ = true;
    state_changed_ = true;
    send_upload_complete();
}

void DesktopProtocol::handle_file_upload_abort() {
    reset_upload_completion();
    upload_abort_cleanup();
    state_changed_ = true;
    emit_ok("FILE_UPLOAD_ABORT");
}

// -- Download handlers --------------------------------------------------------

void DesktopProtocol::handle_file_download(const char* params) {
    if (transfer_active()) {
        char name[64] = {};
        if (download_.active &&
            param_get(params, "NAME", name, sizeof(name)) &&
            std::strcmp(name, download_.name) == 0) {
            send_download_ready();
            if (download_.awaiting_ack) {
                resend_download_chunk();
            }
            return;
        }
        emit_error("DOWNLOAD_BUSY");
        return;
    }
    if (!is_sd_capable_state(msm_.state())) { emit_error("DOWNLOAD_INVALID_STATE"); return; }
    if (!msm_.sd_mounted()) { emit_error("DOWNLOAD_SD_NOT_MOUNTED"); return; }

    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) { emit_error("DOWNLOAD_MISSING_PARAM"); return; }
    if (!is_safe_storage_name(name)) { emit_error("DOWNLOAD_INVALID_FILENAME"); return; }

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    FILINFO info;
    if (f_stat(path, &info) != FR_OK) { emit_error("DOWNLOAD_FILE_NOT_FOUND"); return; }

    download_.file = FIL{};
    if (f_open(&download_.file, path, FA_READ) != FR_OK) { emit_error("DOWNLOAD_SD_READ_FAIL"); return; }

    reset_download_completion();
    download_.active        = true;
    download_.total_size    = info.fsize;
    download_.bytes_sent    = 0;
    download_.crc_running   = 0xFFFFFFFFu;
    download_.next_seq      = 0;
    download_.transfer_id   = next_transfer_id_++;
    download_.awaiting_ack  = false;
    download_.last_chunk_seq = 0;
    download_.last_chunk_len = 0;
    download_.last_send_ms   = 0;
    std::strncpy(download_.name, name, sizeof(download_.name) - 1);
    download_.name[sizeof(download_.name) - 1] = '\0';

    send_download_ready();
    send_next_download_chunk();
}

void DesktopProtocol::handle_file_download_abort() {
    if (!download_.active) {
        emit_ok("FILE_DOWNLOAD_ABORT");
        return;
    }

    f_close(&download_.file);
    download_.active = false;
    download_.awaiting_ack = false;
    reset_download_completion();
    emit_ok("FILE_DOWNLOAD_ABORT");
}

void DesktopProtocol::handle_file_download_ack(const char* params) {
    const uint32_t transfer_id = param_get_u32(params, "ID", 0);
    const uint32_t sequence = param_get_u32(params, "SEQ", 0xFFFFFFFFu);

    if (!download_.active) {
        if (download_.completion_valid &&
            transfer_id == download_.completion_transfer_id &&
            sequence == download_.completion_last_seq) {
            send_download_complete();
        }
        return;
    }

    if (transfer_id != download_.transfer_id) {
        emit_error("DOWNLOAD_INVALID_SESSION");
        return;
    }

    if (!download_.awaiting_ack) {
        return;
    }

    if (sequence < download_.last_chunk_seq) {
        return;
    }

    if (sequence != download_.last_chunk_seq) {
        transport_.send_fmt("@ERROR DOWNLOAD_BAD_SEQUENCE SEQ=%lu EXPECTED=%lu",
                            static_cast<unsigned long>(sequence),
                            static_cast<unsigned long>(download_.last_chunk_seq));
        return;
    }

    download_.awaiting_ack = false;
    send_next_download_chunk();
}

void DesktopProtocol::send_next_download_chunk() {
    UINT bytes_read = 0;
    const FRESULT fr = f_read(&download_.file, chunk_raw_, sizeof(chunk_raw_), &bytes_read);
    if (fr != FR_OK) {
        f_close(&download_.file);
        download_.active = false;
        download_.awaiting_ack = false;
        emit_error("DOWNLOAD_SD_READ_FAIL");
        return;
    }

    if (bytes_read == 0) {
        // EOF -- send end
        const uint32_t final_crc = ~download_.crc_running;
        const uint32_t final_seq = download_.next_seq == 0 ? 0 : (download_.next_seq - 1);
        f_close(&download_.file);
        download_.active = false;
        download_.awaiting_ack = false;
        download_.completion_valid = true;
        download_.completion_crc = final_crc;
        download_.completion_last_seq = final_seq;
        download_.completion_transfer_id = download_.transfer_id;
        std::strncpy(download_.completion_name, download_.name, sizeof(download_.completion_name) - 1);
        download_.completion_name[sizeof(download_.completion_name) - 1] = '\0';
        send_download_complete();
        return;
    }

    download_.crc_running = crc32_update(download_.crc_running, chunk_raw_, bytes_read);
    download_.bytes_sent += bytes_read;
    download_.last_chunk_seq = download_.next_seq;
    download_.last_chunk_len = static_cast<uint16_t>(bytes_read);
    download_.awaiting_ack = true;
    download_.last_send_ms = to_ms_since_boot(get_absolute_time());

    transport_.send_frame(kDownloadDataFrameType,
                          download_.transfer_id,
                          download_.last_chunk_seq,
                          chunk_raw_,
                          static_cast<uint16_t>(bytes_read));
    download_.next_seq++;
}

// -- Upload helpers -----------------------------------------------------------

void DesktopProtocol::upload_abort_cleanup() {
    if (!upload_.active) return;
    f_close(&upload_.file);
    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", upload_.name);
    f_unlink(path);
    upload_.active = false;
    upload_.last_ack_valid = false;
}

void DesktopProtocol::handle_upload_chunk_data(uint8_t transfer_id,
                                               uint32_t sequence,
                                               const uint8_t* payload,
                                               uint16_t payload_len) {
    if (!upload_.active) {
        if (upload_.completion_valid && transfer_id == upload_.completion_transfer_id) {
            send_upload_complete();
        }
        return;
    }
    if (transfer_id != upload_.transfer_id) {
        emit_error("UPLOAD_INVALID_SESSION");
        return;
    }
    if (upload_.last_ack_valid && sequence == upload_.last_ack_seq) {
        send_upload_chunk_ack(upload_.last_ack_seq, upload_.last_ack_bytes);
        return;
    }
    if (sequence != upload_.expected_seq) {
        transport_.send_fmt("@ERROR UPLOAD_BAD_SEQUENCE SEQ=%lu EXPECTED=%lu",
                            static_cast<unsigned long>(sequence),
                            static_cast<unsigned long>(upload_.expected_seq));
        return;
    }
    if (payload == nullptr || payload_len == 0 || payload_len > kTransferRawChunkSize) {
        transport_.send_fmt("@ERROR CHUNK SEQ=%lu REASON=BAD_LENGTH",
                            static_cast<unsigned long>(sequence));
        return;
    }
    if (upload_.expected_size > 0 &&
        upload_.bytes_written + payload_len > upload_.expected_size) {
        upload_abort_cleanup();
        transport_.send_fmt("@ERROR UPLOAD_SIZE_MISMATCH SEQ=%lu",
                            static_cast<unsigned long>(sequence));
        state_changed_ = true;
        return;
    }

    UINT written = 0;
    FRESULT fr = f_write(&upload_.file, payload, payload_len, &written);
    if (fr != FR_OK || written != payload_len) {
        upload_abort_cleanup();
        transport_.send_fmt("@ERROR UPLOAD_SD_WRITE_FAIL SEQ=%lu",
                            static_cast<unsigned long>(sequence));
        state_changed_ = true;
        return;
    }

    upload_.crc_running = crc32_update(upload_.crc_running, payload, payload_len);
    upload_.bytes_written += payload_len;
    upload_.expected_seq++;
    upload_.last_ack_valid = true;
    upload_.last_ack_seq = sequence;
    upload_.last_ack_bytes = upload_.bytes_written;
    send_upload_chunk_ack(sequence, upload_.bytes_written);
}

// -- Storage callbacks --------------------------------------------------------

void DesktopProtocol::on_sd_mounted() {
    inject(MachineEvent::SdMounted);
    emit_state_update();
    emit_event("SD_MOUNTED");
}

void DesktopProtocol::on_sd_removed() {
    if (upload_.active) {
        reset_upload_completion();
        upload_abort_cleanup();
        emit_error("UPLOAD_SD_REMOVED");
        state_changed_ = true;
    }
    if (download_.active) {
        f_close(&download_.file);
        download_.active = false;
        download_.awaiting_ack = false;
        reset_download_completion();
        emit_error("DOWNLOAD_SD_REMOVED");
    }
    inject(MachineEvent::SdRemoved);
    jobs_.handle_event(JobEvent::ClearLoadedFile);
    emit_state_update();
    emit_job();
    emit_event("SD_REMOVED");
}

// -- Param helpers ------------------------------------------------------------

bool DesktopProtocol::param_get(const char* params, const char* key,
                                 char* out, size_t max) {
    if (!params || !key || !out || max == 0) return false;

    size_t key_len = std::strlen(key);
    const char* p = params;

    while (*p) {
        // Skip leading spaces
        while (*p == ' ') p++;
        if (*p == '\0') break;

        // Check if this token starts with "KEY="
        if (std::strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char* val_start = p + key_len + 1;
            // Value runs until next space or end of string
            const char* val_end = val_start;
            while (*val_end && *val_end != ' ') val_end++;
            size_t val_len = static_cast<size_t>(val_end - val_start);
            size_t copy = (val_len < max - 1) ? val_len : (max - 1);
            std::memcpy(out, val_start, copy);
            out[copy] = '\0';
            percent_decode_in_place(out);
            return true;
        }

        // Advance past this token
        while (*p && *p != ' ') p++;
    }
    return false;
}

uint32_t DesktopProtocol::param_get_u32(const char* params, const char* key,
                                         uint32_t def) {
    char buf[16] = {};
    if (!param_get(params, key, buf, sizeof(buf))) return def;
    return (uint32_t)std::strtoul(buf, nullptr, 10);
}

int DesktopProtocol::param_get_int(const char* params, const char* key, int def) {
    char buf[16] = {};
    if (!param_get(params, key, buf, sizeof(buf))) return def;
    return std::atoi(buf);
}

// -- Encoding helpers ---------------------------------------------------------

uint32_t DesktopProtocol::crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc;
}
