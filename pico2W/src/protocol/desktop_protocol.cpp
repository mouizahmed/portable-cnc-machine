#include "protocol/desktop_protocol.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

#include "pico/multicore.h"
#include "pico/stdlib.h"

static void percent_decode_in_place(char* text);
static void percent_encode_value(const char* input, char* output, size_t output_size);
static uint32_t crc32_update_table(uint32_t crc, const uint8_t* data, size_t len);

DesktopProtocol* DesktopProtocol::storage_worker_instance_ = nullptr;

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
      sd_(sd) {
    critical_section_init(&upload_worker_lock_);
    if (storage_worker_instance_ == nullptr) {
        storage_worker_instance_ = this;
        multicore_launch_core1(&DesktopProtocol::storage_worker_entry);
    }
}

// -- Poll / dispatch ----------------------------------------------------------

void DesktopProtocol::poll() {
    while (true) {
        const auto kind = transport_.poll(line_, sizeof(line_), frame_);
        if (kind == UsbCdcTransport::PacketKind::None) {
            tick_transfer_retries();
            process_upload_results();
            return;
        }
        if (kind == UsbCdcTransport::PacketKind::Line) {
            dispatch(line_);
        } else if (kind == UsbCdcTransport::PacketKind::Frame) {
            dispatch_frame(frame_);
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
        const auto& ctx = transfer_.context();

        if (!transfer_.is_download()) {
            if (ctx.completion_valid &&
                ctx.completion_operation == StorageTransferOperation::Download &&
                frame.transfer_id == ctx.completion_session_id &&
                frame.seq == ctx.completion_last_seq) {
                send_download_complete();
            }
            return;
        }
        if (frame.transfer_id != ctx.session_id) {
            emit_storage_error(StorageTransferError::InvalidSession, StorageTransferOperation::Download);
            return;
        }
        if (ctx.chunks_in_flight == 0) {
            return;
        }
        const uint32_t oldest = ctx.expected_sequence - ctx.chunks_in_flight;
        const uint32_t newest = ctx.expected_sequence - 1u;
        if (frame.seq < oldest) {
            return;  // Old/duplicate ACK
        }
        if (frame.seq > newest) {
            char kv[64];
            std::snprintf(kv, sizeof(kv), "SEQ=%lu EXPECTED=%lu",
                          static_cast<unsigned long>(frame.seq),
                          static_cast<unsigned long>(newest));
            emit_storage_error(StorageTransferError::BadSequence, StorageTransferOperation::Download, kv);
            return;
        }
        transfer_.note_download_ack(frame.seq);
        transfer_.set_last_send_ms(to_ms_since_boot(get_absolute_time()));
        fill_download_window();
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

void DesktopProtocol::emit_storage_error(StorageTransferError error,
                                         StorageTransferOperation operation,
                                         const char* kv) {
    const char* reason = "STORAGE_ERROR";
    switch (error) {
        case StorageTransferError::Busy:            reason = "STORAGE_BUSY"; break;
        case StorageTransferError::NotAllowed:      reason = "STORAGE_NOT_ALLOWED"; break;
        case StorageTransferError::SdNotMounted:    reason = "STORAGE_NO_SD"; break;
        case StorageTransferError::FileNotFound:    reason = "STORAGE_FILE_NOT_FOUND"; break;
        case StorageTransferError::InvalidFilename: reason = "STORAGE_INVALID_FILENAME"; break;
        case StorageTransferError::InvalidSession:  reason = "STORAGE_INVALID_SESSION"; break;
        case StorageTransferError::BadSequence:     reason = "STORAGE_BAD_SEQUENCE"; break;
        case StorageTransferError::SizeMismatch:    reason = "STORAGE_SIZE_MISMATCH"; break;
        case StorageTransferError::CrcMismatch:     reason = "STORAGE_CRC_FAIL"; break;
        case StorageTransferError::ReadFail:        reason = "STORAGE_READ_FAIL"; break;
        case StorageTransferError::WriteFail:       reason = "STORAGE_WRITE_FAIL"; break;
        case StorageTransferError::NoSpace:         reason = "STORAGE_NO_SPACE"; break;
        case StorageTransferError::Aborted:         reason = "STORAGE_ABORTED"; break;
        case StorageTransferError::None:            break;
    }

    if (operation == StorageTransferOperation::None) {
        operation = transfer_.operation();
        if (operation == StorageTransferOperation::None &&
            transfer_.context().completion_valid) {
            operation = transfer_.context().completion_operation;
        }
    }

    const char* op = nullptr;
    switch (operation) {
        case StorageTransferOperation::List:     op = "LIST"; break;
        case StorageTransferOperation::Load:     op = "LOAD"; break;
        case StorageTransferOperation::Unload:   op = "UNLOAD"; break;
        case StorageTransferOperation::Delete:   op = "DELETE"; break;
        case StorageTransferOperation::Upload:   op = "UPLOAD"; break;
        case StorageTransferOperation::Download: op = "DOWNLOAD"; break;
        case StorageTransferOperation::None:     break;
    }

    if (op != nullptr && kv != nullptr && kv[0] != '\0') {
        transport_.send_fmt("@ERROR %s OP=%s %s", reason, op, kv);
    } else if (op != nullptr) {
        transport_.send_fmt("@ERROR %s OP=%s", reason, op);
    } else if (kv != nullptr && kv[0] != '\0') {
        transport_.send_fmt("@ERROR %s %s", reason, kv);
    } else {
        transport_.send_fmt("@ERROR %s", reason);
    }
}

void DesktopProtocol::abort_active_storage_transfer(bool clear_completion,
                                                    bool delete_partial_upload_file,
                                                    bool close_file) {
    if (!transfer_.is_active()) {
        if (clear_completion) {
            transfer_.clear_completion();
        }
        return;
    }

    const auto operation = transfer_.operation();
    const auto ctx = transfer_.context();
    if (close_file && (operation == StorageTransferOperation::Upload ||
                       operation == StorageTransferOperation::Download)) {
        f_close(&transfer_.file());
    }

    if (delete_partial_upload_file && operation == StorageTransferOperation::Upload) {
        char path[80];
        std::snprintf(path, sizeof(path), "0:/%s", ctx.filename);
        f_unlink(path);
    }

    if (operation == StorageTransferOperation::Upload) {
        reset_upload_queue();
    }

    transfer_.finish_operation();

    if (clear_completion) {
        transfer_.clear_completion();
    }
}

void DesktopProtocol::reset_upload_completion() {
    if (transfer_.context().completion_operation == StorageTransferOperation::Upload) {
        transfer_.clear_completion();
    }
}

void DesktopProtocol::reset_download_completion() {
    if (transfer_.context().completion_operation == StorageTransferOperation::Download) {
        transfer_.clear_completion();
    }
}

void DesktopProtocol::send_upload_ready() {
    const auto& ctx = transfer_.context();
    char encoded_name[192]{};
    percent_encode_value(ctx.filename, encoded_name, sizeof(encoded_name));
    char kv[256];
    std::snprintf(kv, sizeof(kv), "NAME=%s SIZE=%lu ID=%u CHUNK=%u",
                  encoded_name,
                  static_cast<unsigned long>(ctx.expected_size),
                  static_cast<unsigned int>(ctx.session_id),
                  static_cast<unsigned int>(kTransferRawChunkSize));
    emit_ok_kv("FILE_UPLOAD_READY", kv);
}

void DesktopProtocol::send_upload_chunk_ack(uint32_t sequence, uint32_t bytes_committed) {
    uint8_t ack_payload[sizeof(uint32_t)]{};
    ack_payload[0] = static_cast<uint8_t>(bytes_committed & 0xFFu);
    ack_payload[1] = static_cast<uint8_t>((bytes_committed >> 8) & 0xFFu);
    ack_payload[2] = static_cast<uint8_t>((bytes_committed >> 16) & 0xFFu);
    ack_payload[3] = static_cast<uint8_t>((bytes_committed >> 24) & 0xFFu);
    transport_.send_frame(kUploadAckFrameType, transfer_.session_id(), sequence, ack_payload, sizeof(ack_payload));
}

void DesktopProtocol::send_upload_complete() {
    const auto& ctx = transfer_.context();
    char encoded_name[192]{};
    percent_encode_value(ctx.completion_name, encoded_name, sizeof(encoded_name));
    transport_.send_fmt("@OK FILE_UPLOAD_END NAME=%s SIZE=%lu ID=%u",
                        encoded_name,
                        static_cast<unsigned long>(ctx.completion_size),
                        static_cast<unsigned int>(ctx.completion_session_id));
}

void DesktopProtocol::send_download_ready() {
    const auto& ctx = transfer_.context();
    char encoded_name[192]{};
    percent_encode_value(ctx.filename, encoded_name, sizeof(encoded_name));
    char kv[256];
    std::snprintf(kv, sizeof(kv), "NAME=%s SIZE=%lu ID=%u CHUNK=%u",
                  encoded_name,
                  static_cast<unsigned long>(ctx.expected_size),
                  static_cast<unsigned int>(ctx.session_id),
                  static_cast<unsigned int>(kTransferRawChunkSize));
    emit_ok_kv("FILE_DOWNLOAD_READY", kv);
}

void DesktopProtocol::send_download_complete() {
    const auto& ctx = transfer_.context();
    char encoded_name[192]{};
    percent_encode_value(ctx.completion_name, encoded_name, sizeof(encoded_name));
    transport_.send_fmt("@OK FILE_DOWNLOAD_END NAME=%s CRC=%08lx ID=%u",
                        encoded_name,
                        static_cast<unsigned long>(ctx.completion_crc),
                        static_cast<unsigned int>(ctx.completion_session_id));
}

void DesktopProtocol::fill_download_window() {
    while (transfer_.is_download()) {
        if (transfer_.chunks_in_flight() >= kDownloadWindowSize) break;
        send_next_download_chunk();
    }
}

void DesktopProtocol::tick_transfer_retries() {
    const auto& ctx = transfer_.context();
    if (!transfer_.is_download() || ctx.chunks_in_flight == 0) {
        return;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - ctx.last_send_ms) >= kDownloadResendIntervalMs) {
        abort_active_storage_transfer(true, false);
        emit_storage_error(StorageTransferError::Aborted, StorageTransferOperation::Download);
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
    if (!transfer_.begin_listing(msm_.state())) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::List);
        return;
    }
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::List);
        return;
    }

    DIR dir;
    FILINFO info;
    FRESULT fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::List);
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
    transfer_.finish_operation();
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
    if (!storage_transfer_machine_state_is_allowed(msm_.state()) || !msm_.sd_mounted()) {
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
    if (!storage_transfer_machine_state_is_allowed(msm_.state()) || !jobs_.has_loaded_job()) {
        return false;
    }

    if (!jobs_.handle_event(JobEvent::ClearLoadedFile)) {
        return false;
    }

    inject(MachineEvent::JobUnloaded);
    return true;
}

void DesktopProtocol::handle_file_load(const char* params) {
    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("MISSING_PARAM");
        return;
    }
    if (!is_safe_storage_name(name)) {
        emit_storage_error(StorageTransferError::InvalidFilename, StorageTransferOperation::Load);
        return;
    }
    if (!transfer_.begin_loading(msm_.state(), name)) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::Load);
        return;
    }
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Load);
        return;
    }

    const int16_t index = find_job_index_by_name(name);
    if (index < 0 || !load_job_by_index(index)) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::FileNotFound, StorageTransferOperation::Load);
        return;
    }

    char encoded_name[192]{};
    percent_encode_value(name, encoded_name, sizeof(encoded_name));
    char kv[220];
    std::snprintf(kv, sizeof(kv), "NAME=%s", encoded_name);
    emit_ok_kv("FILE_LOAD", kv);
    transfer_.finish_operation();
}

void DesktopProtocol::handle_file_unload() {
    if (!transfer_.begin_unload(msm_.state())) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::Unload);
        return;
    }

    if (!unload_job()) {
        transfer_.finish_operation();
        emit_error("NO_JOB_LOADED");
        return;
    }

    emit_ok("FILE_UNLOAD");
    transfer_.finish_operation();
}

void DesktopProtocol::handle_file_delete(const char* params) {
    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("MISSING_PARAM");
        return;
    }
    if (!is_safe_storage_name(name)) {
        emit_storage_error(StorageTransferError::InvalidFilename, StorageTransferOperation::Delete);
        return;
    }
    if (!transfer_.begin_deleting(msm_.state(), name)) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::Delete);
        return;
    }
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Delete);
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
        transfer_.finish_operation();
    } else {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::FileNotFound, StorageTransferOperation::Delete);
    }
}

void DesktopProtocol::handle_file_upload(const char* params) {
    if (transfer_active()) {
        char name[64] = {};
        const uint32_t size = param_get_u32(params, "SIZE", 0);
        if (transfer_.is_upload() &&
            param_get(params, "NAME", name, sizeof(name)) &&
            std::strcmp(name, transfer_.filename()) == 0 &&
            size == transfer_.expected_size()) {
            send_upload_ready();
            return;
        }
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::Upload);
        return;
    }
    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("UPLOAD_MISSING_PARAM");
        return;
    }
    if (!is_safe_storage_name(name)) {
        emit_storage_error(StorageTransferError::InvalidFilename, StorageTransferOperation::Upload);
        return;
    }

    uint32_t size      = param_get_u32(params, "SIZE", 0);
    constexpr uint32_t kMaxUploadBytes = 5u * 1024u * 1024u;
    if (size > kMaxUploadBytes) {
        emit_storage_error(StorageTransferError::NoSpace, StorageTransferOperation::Upload);
        return;
    }
    const uint8_t transfer_id = next_transfer_id_;
    if (!transfer_.begin_upload(msm_.state(), name, size, transfer_id)) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::Upload);
        return;
    }
    next_transfer_id_++;
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Upload);
        return;
    }

    int      overwrite = param_get_int(params, "OVERWRITE", 0);

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    // Check if file already exists
    FILINFO info;
    FRESULT stat_r = f_stat(path, &info);
    if (stat_r == FR_OK && !overwrite) {
        transfer_.finish_operation();
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
        transfer_.finish_operation();
        char kv[64];
        std::snprintf(kv, sizeof(kv), "FREE=%llu", (unsigned long long)free_bytes);
        emit_storage_error(StorageTransferError::NoSpace, StorageTransferOperation::Upload, kv);
        return;
    }

    reset_upload_completion();
    transfer_.set_crc_running(0xFFFFFFFFu);
    transfer_.file() = FIL{};
    upload_profile_start_ms_ = to_ms_since_boot(get_absolute_time());
    upload_profile_prealloc_ms_ = 0;
    upload_profile_write_ms_ = 0;
    upload_profile_max_write_ms_ = 0;
    upload_profile_close_ms_ = 0;
    upload_profile_chunks_ = 0;
    reset_upload_queue();
    upload_worker_enabled_ = false;

    // Open file for writing
    FRESULT fr = f_open(&transfer_.file(), path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload);
        return;
    }

    transfer_.set_expected_sequence(0);
    transfer_.set_last_ack_sequence(0xFFFFFFFFu);
    transfer_.set_retry_count(0);

    state_changed_ = true;
    send_upload_ready();
}

void DesktopProtocol::handle_file_upload_end(const char* params) {
    const auto& ctx = transfer_.context();
    if (!transfer_.is_upload()) {
        char crc_str[16] = {};
        if (param_get(params, "CRC", crc_str, sizeof(crc_str))) {
            const uint32_t expected_crc = static_cast<uint32_t>(std::strtoul(crc_str, nullptr, 16));
            if (ctx.completion_valid &&
                ctx.completion_operation == StorageTransferOperation::Upload &&
                expected_crc == ctx.completion_crc) {
                send_upload_complete();
                return;
            }
        }
        emit_storage_error(StorageTransferError::InvalidSession, StorageTransferOperation::Upload);
        return;
    }

    char crc_str[16] = {};
    param_get(params, "CRC", crc_str, sizeof(crc_str));

    const uint32_t finalize_wait_start = to_ms_since_boot(get_absolute_time());
    while (ctx.bytes_written < ctx.expected_size) {
        process_upload_results();
        bool pending = false;
        critical_section_enter_blocking(&upload_worker_lock_);
        pending = upload_queue_count_ > 0 || upload_result_count_ > 0 || upload_worker_busy_;
        critical_section_exit(&upload_worker_lock_);
        if (!pending) {
            break;
        }
        if ((to_ms_since_boot(get_absolute_time()) - finalize_wait_start) > 10000u) {
            abort_active_storage_transfer(false, true);
            state_changed_ = true;
            emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload);
            return;
        }
        sleep_us(100);
    }

    if (ctx.expected_size > 0 && ctx.bytes_written != ctx.expected_size) {
        abort_active_storage_transfer(false, true);
        state_changed_ = true;
        emit_storage_error(StorageTransferError::SizeMismatch, StorageTransferOperation::Upload);
        return;
    }

    uint32_t final_crc = ~ctx.crc_running;
    uint32_t expected_crc = (uint32_t)std::strtoul(crc_str, nullptr, 16);

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", ctx.filename);

    if (final_crc != expected_crc) {
        char kv[128];
        std::snprintf(kv,
                      sizeof(kv),
                      "PHASE=RECEIVE EXPECTED=%08lx ACTUAL=%08lx WRITTEN=%lu SIZE=%lu",
                      static_cast<unsigned long>(expected_crc),
                      static_cast<unsigned long>(final_crc),
                      static_cast<unsigned long>(ctx.bytes_written),
                      static_cast<unsigned long>(ctx.expected_size));
        abort_active_storage_transfer(false, true);
        state_changed_ = true;
        emit_storage_error(StorageTransferError::CrcMismatch, StorageTransferOperation::Upload, kv);
        return;
    }

    char name_copy[64];
    uint32_t bytes_copy = ctx.bytes_written;
    std::strncpy(name_copy, ctx.filename, sizeof(name_copy));
    name_copy[sizeof(name_copy) - 1] = '\0';
    const uint8_t session_id = ctx.session_id;

    const uint32_t close_start_ms = to_ms_since_boot(get_absolute_time());
    FRESULT close_result = f_sync(&transfer_.file());
    if (close_result == FR_OK) {
        close_result = f_close(&transfer_.file());
    } else {
        f_close(&transfer_.file());
    }
    upload_profile_close_ms_ = to_ms_since_boot(get_absolute_time()) - close_start_ms;
    if (close_result != FR_OK) {
        char kv[96];
        std::snprintf(kv,
                      sizeof(kv),
                      "PHASE=CLOSE FRESULT=%d WRITTEN=%lu SIZE=%lu",
                      static_cast<int>(close_result),
                      static_cast<unsigned long>(bytes_copy),
                      static_cast<unsigned long>(ctx.expected_size));
        f_unlink(path);
        transfer_.finish_operation();
        reset_upload_queue();
        state_changed_ = true;
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
        return;
    }

    constexpr int kVerifyPasses = 3;
    uint32_t verify_crc[kVerifyPasses]{};
    uint32_t verify_size[kVerifyPasses]{};
    FRESULT verify_result[kVerifyPasses]{};
    uint32_t first_mismatch_offset = 0xFFFFFFFFu;
    uint8_t first_mismatch_expected = 0;
    uint8_t first_mismatch_actual = 0;
    const bool can_compare_frame_buffer =
        upload_profile_chunks_ == 1u && bytes_copy <= frame_.payload_len;
    const uint32_t frame_compare_len = can_compare_frame_buffer
        ? bytes_copy
        : static_cast<uint32_t>(frame_.payload_len);
    const uint32_t frame_crc = frame_compare_len > 0
        ? ~crc32_update(0xFFFFFFFFu, frame_.payload, frame_compare_len)
        : 0u;

    for (int pass = 0; pass < kVerifyPasses; ++pass) {
        FIL verify_file{};
        verify_result[pass] = f_open(&verify_file, path, FA_READ);
        uint32_t running_crc = 0xFFFFFFFFu;
        if (verify_result[pass] == FR_OK) {
            while (true) {
                UINT bytes_read = 0;
                verify_result[pass] = f_read(&verify_file, chunk_raw_, sizeof(chunk_raw_), &bytes_read);
                if (verify_result[pass] != FR_OK || bytes_read == 0) {
                    break;
                }

                if (pass == 0 && can_compare_frame_buffer && first_mismatch_offset == 0xFFFFFFFFu) {
                    for (UINT index = 0; index < bytes_read; ++index) {
                        const uint32_t offset = verify_size[pass] + index;
                        if (offset >= bytes_copy) {
                            break;
                        }
                        const uint8_t expected_byte = frame_.payload[offset];
                        if (chunk_raw_[index] != expected_byte) {
                            first_mismatch_offset = offset;
                            first_mismatch_expected = expected_byte;
                            first_mismatch_actual = chunk_raw_[index];
                            break;
                        }
                    }
                }

                running_crc = crc32_update(running_crc, chunk_raw_, bytes_read);
                verify_size[pass] += bytes_read;
                if (verify_size[pass] > ctx.expected_size) {
                    verify_result[pass] = FR_INT_ERR;
                    break;
                }
            }
            f_close(&verify_file);
        }
        verify_crc[pass] = ~running_crc;
    }

    const uint32_t verified_final_crc = verify_crc[0];
    if (verify_result[0] != FR_OK) {
        char kv[128];
        std::snprintf(kv,
                      sizeof(kv),
                      "PHASE=READBACK FRESULT=%d READ=%lu SIZE=%lu",
                      static_cast<int>(verify_result[0]),
                      static_cast<unsigned long>(verify_size[0]),
                      static_cast<unsigned long>(ctx.expected_size));
        f_unlink(path);
        transfer_.finish_operation();
        reset_upload_queue();
        state_changed_ = true;
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::Upload, kv);
        return;
    }

    if (verify_size[0] != bytes_copy) {
        char kv[128];
        std::snprintf(kv,
                      sizeof(kv),
                      "PHASE=READBACK EXPECTED=%lu ACTUAL=%lu",
                      static_cast<unsigned long>(bytes_copy),
                      static_cast<unsigned long>(verify_size[0]));
        f_unlink(path);
        transfer_.finish_operation();
        reset_upload_queue();
        state_changed_ = true;
        emit_storage_error(StorageTransferError::SizeMismatch, StorageTransferOperation::Upload, kv);
        return;
    }

    if (verified_final_crc != expected_crc) {
        char kv[256];
        if (first_mismatch_offset != 0xFFFFFFFFu) {
            std::snprintf(kv,
                          sizeof(kv),
                          "PHASE=READBACK EXPECTED=%08lx ACTUAL=%08lx ACTUAL2=%08lx ACTUAL3=%08lx SIZE=%lu FRAMECRC=%08lx FRAMELEN=%lu CHUNKS=%lu DIFF=%lu WANT=%02x GOT=%02x",
                          static_cast<unsigned long>(expected_crc),
                          static_cast<unsigned long>(verified_final_crc),
                          static_cast<unsigned long>(verify_crc[1]),
                          static_cast<unsigned long>(verify_crc[2]),
                          static_cast<unsigned long>(verify_size[0]),
                          static_cast<unsigned long>(frame_crc),
                          static_cast<unsigned long>(frame_.payload_len),
                          static_cast<unsigned long>(upload_profile_chunks_),
                          static_cast<unsigned long>(first_mismatch_offset),
                          static_cast<unsigned int>(first_mismatch_expected),
                          static_cast<unsigned int>(first_mismatch_actual));
        } else {
            std::snprintf(kv,
                          sizeof(kv),
                          "PHASE=READBACK EXPECTED=%08lx ACTUAL=%08lx ACTUAL2=%08lx ACTUAL3=%08lx SIZE=%lu FRAMECRC=%08lx FRAMELEN=%lu CHUNKS=%lu",
                          static_cast<unsigned long>(expected_crc),
                          static_cast<unsigned long>(verified_final_crc),
                          static_cast<unsigned long>(verify_crc[1]),
                          static_cast<unsigned long>(verify_crc[2]),
                          static_cast<unsigned long>(verify_size[0]),
                          static_cast<unsigned long>(frame_crc),
                          static_cast<unsigned long>(frame_.payload_len),
                          static_cast<unsigned long>(upload_profile_chunks_));
        }
        f_unlink(path);
        transfer_.finish_operation();
        reset_upload_queue();
        state_changed_ = true;
        emit_storage_error(StorageTransferError::CrcMismatch, StorageTransferOperation::Upload, kv);
        return;
    }

    const uint32_t total_ms = to_ms_since_boot(get_absolute_time()) - upload_profile_start_ms_;
    const uint32_t bps = total_ms > 0
        ? static_cast<uint32_t>((static_cast<uint64_t>(bytes_copy) * 1000u) / total_ms)
        : 0u;

    char profile_kv[192];
    std::snprintf(profile_kv,
                  sizeof(profile_kv),
                  "SIZE=%lu TOTAL_MS=%lu PREALLOC_MS=%lu WRITE_MS=%lu MAX_WRITE_MS=%lu CLOSE_MS=%lu CHUNKS=%lu QUEUE_MAX=%lu BPS=%lu",
                  static_cast<unsigned long>(bytes_copy),
                  static_cast<unsigned long>(total_ms),
                  static_cast<unsigned long>(upload_profile_prealloc_ms_),
                  static_cast<unsigned long>(upload_profile_write_ms_),
                  static_cast<unsigned long>(upload_profile_max_write_ms_),
                  static_cast<unsigned long>(upload_profile_close_ms_),
                  static_cast<unsigned long>(upload_profile_chunks_),
                  static_cast<unsigned long>(upload_queue_high_water_),
                  static_cast<unsigned long>(bps));
    emit_event_kv("STORAGE_UPLOAD_PROFILE", profile_kv);

    transfer_.finish_operation();
    reset_upload_queue();
    transfer_.set_completion(StorageTransferOperation::Upload,
                             name_copy,
                             bytes_copy,
                             final_crc,
                             session_id,
                             0);
    file_list_changed_ = true;
    state_changed_ = true;
    send_upload_complete();
}

void DesktopProtocol::handle_file_upload_abort() {
    abort_active_storage_transfer(true, true);
    state_changed_ = true;
    emit_ok("FILE_UPLOAD_ABORT");
}

// -- Download handlers --------------------------------------------------------

void DesktopProtocol::handle_file_download(const char* params) {
    if (transfer_active()) {
        char name[64] = {};
        if (transfer_.is_download() &&
            param_get(params, "NAME", name, sizeof(name)) &&
            std::strcmp(name, transfer_.filename()) == 0) {
            send_download_ready();
            fill_download_window();
            return;
        }
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::Download);
        return;
    }
    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) { emit_error("DOWNLOAD_MISSING_PARAM"); return; }
    if (!is_safe_storage_name(name)) {
        emit_storage_error(StorageTransferError::InvalidFilename, StorageTransferOperation::Download);
        return;
    }

    const uint8_t transfer_id = next_transfer_id_;
    if (!transfer_.begin_download(msm_.state(), name, 0, transfer_id)) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::Download);
        return;
    }
    next_transfer_id_++;
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Download);
        return;
    }

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    FILINFO info;
    if (f_stat(path, &info) != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::FileNotFound, StorageTransferOperation::Download);
        return;
    }

    reset_download_completion();
    transfer_.set_expected_size(info.fsize);
    transfer_.file() = FIL{};
    if (f_open(&transfer_.file(), path, FA_READ) != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::Download);
        return;
    }

    transfer_.set_expected_sequence(0);
    transfer_.set_crc_running(0xFFFFFFFFu);
    transfer_.set_retry_count(0);
    transfer_.set_awaiting_ack(false);
    transfer_.set_last_chunk_length(0);
    transfer_.set_last_send_ms(0);

    send_download_ready();
    fill_download_window();
}

void DesktopProtocol::handle_file_download_abort() {
    if (!transfer_.is_download()) {
        emit_ok("FILE_DOWNLOAD_ABORT");
        return;
    }

    abort_active_storage_transfer(true, false);
    emit_ok("FILE_DOWNLOAD_ABORT");
}

void DesktopProtocol::handle_file_download_ack(const char* params) {
    const uint32_t transfer_id = param_get_u32(params, "ID", 0);
    const uint32_t sequence = param_get_u32(params, "SEQ", 0xFFFFFFFFu);
    const auto& ctx = transfer_.context();

    if (!transfer_.is_download()) {
        if (ctx.completion_valid &&
            ctx.completion_operation == StorageTransferOperation::Download &&
            transfer_id == ctx.completion_session_id &&
            sequence == ctx.completion_last_seq) {
            send_download_complete();
        }
        return;
    }

    if (transfer_id != ctx.session_id) {
        emit_storage_error(StorageTransferError::InvalidSession, StorageTransferOperation::Download);
        return;
    }

    if (ctx.chunks_in_flight == 0) {
        return;
    }

    const uint32_t oldest = ctx.expected_sequence - ctx.chunks_in_flight;
    const uint32_t newest = ctx.expected_sequence - 1u;
    if (sequence < oldest) {
        return;  // Old/duplicate ACK
    }

    if (sequence > newest) {
        char kv[64];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu EXPECTED=%lu",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned long>(newest));
        emit_storage_error(StorageTransferError::BadSequence, StorageTransferOperation::Download, kv);
        return;
    }

    transfer_.note_download_ack(sequence);
    transfer_.set_last_send_ms(to_ms_since_boot(get_absolute_time()));
    fill_download_window();
}

void DesktopProtocol::send_next_download_chunk() {
    const auto& ctx = transfer_.context();
    UINT bytes_read = 0;
    const FRESULT fr = f_read(&transfer_.file(), chunk_raw_, sizeof(chunk_raw_), &bytes_read);
    if (fr != FR_OK) {
        abort_active_storage_transfer(false, false);
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::Download);
        return;
    }

    if (bytes_read == 0) {
        // EOF -- send end
        const uint32_t final_crc = ~ctx.crc_running;
        const uint32_t final_seq = ctx.expected_sequence == 0 ? 0u : (ctx.expected_sequence - 1u);
        char name_copy[64]{};
        std::strncpy(name_copy, ctx.filename, sizeof(name_copy) - 1);
        const uint8_t session_id = ctx.session_id;
        const uint32_t size = ctx.bytes_sent;
        f_close(&transfer_.file());
        transfer_.finish_operation();
        transfer_.set_completion(StorageTransferOperation::Download,
                                 name_copy,
                                 size,
                                 final_crc,
                                 session_id,
                                 final_seq);
        send_download_complete();
        return;
    }

    const uint32_t next_seq = ctx.expected_sequence;
    const uint32_t next_crc = crc32_update(ctx.crc_running, chunk_raw_, bytes_read);
    const uint32_t bytes_sent = ctx.bytes_sent + bytes_read;
    const uint32_t send_time = to_ms_since_boot(get_absolute_time());

    transport_.send_frame(kDownloadDataFrameType,
                          ctx.session_id,
                          next_seq,
                          chunk_raw_,
                          static_cast<uint16_t>(bytes_read));
    transfer_.note_download_chunk_sent(next_seq,
                                       static_cast<uint16_t>(bytes_read),
                                       bytes_sent,
                                       next_crc,
                                       send_time);
}

// -- Upload helpers -----------------------------------------------------------

void DesktopProtocol::upload_abort_cleanup() {
    if (!transfer_.is_upload()) return;
    abort_active_storage_transfer(false, true);
}

void DesktopProtocol::handle_upload_chunk_data(uint8_t transfer_id,
                                               uint32_t sequence,
                                               const uint8_t* payload,
                                               uint16_t payload_len) {
    process_upload_results();
    enqueue_upload_chunk(transfer_id, sequence, payload, payload_len);
}

bool DesktopProtocol::enqueue_upload_chunk(uint8_t transfer_id,
                                           uint32_t sequence,
                                           const uint8_t* payload,
                                           uint16_t payload_len) {
    const auto& ctx = transfer_.context();
    if (!transfer_.is_upload()) {
        if (ctx.completion_valid &&
            ctx.completion_operation == StorageTransferOperation::Upload &&
            transfer_id == ctx.completion_session_id) {
            send_upload_complete();
        }
        return false;
    }
    if (transfer_id != ctx.session_id) {
        emit_storage_error(StorageTransferError::InvalidSession, StorageTransferOperation::Upload);
        return false;
    }
    if (sequence < upload_next_receive_sequence_) {
        if (ctx.last_ack_sequence != 0xFFFFFFFFu) {
            send_upload_chunk_ack(ctx.last_ack_sequence, ctx.bytes_written);
        }
        return false;
    }
    if (sequence > upload_next_receive_sequence_) {
        char kv[64];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu EXPECTED=%lu",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned long>(upload_next_receive_sequence_));
        emit_storage_error(StorageTransferError::BadSequence, StorageTransferOperation::Upload, kv);
        return false;
    }
    if (payload == nullptr || payload_len == 0 || payload_len > kTransferRawChunkSize) {
        char kv[64];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu REASON=BAD_LENGTH",
                      static_cast<unsigned long>(sequence));
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
        return false;
    }
    if (ctx.expected_size > 0 &&
        upload_bytes_received_ + payload_len > ctx.expected_size) {
        upload_abort_cleanup();
        char kv[32];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu", static_cast<unsigned long>(sequence));
        emit_storage_error(StorageTransferError::SizeMismatch, StorageTransferOperation::Upload, kv);
        state_changed_ = true;
        return false;
    }
    const uint32_t next_crc = crc32_update(ctx.crc_running, payload, payload_len);

    UINT written = 0;
    const uint32_t write_start_ms = to_ms_since_boot(get_absolute_time());
    const FRESULT fr = f_write(&transfer_.file(), payload, payload_len, &written);
    const uint32_t write_elapsed_ms = to_ms_since_boot(get_absolute_time()) - write_start_ms;

    upload_profile_write_ms_ += write_elapsed_ms;
    if (write_elapsed_ms > upload_profile_max_write_ms_) {
        upload_profile_max_write_ms_ = write_elapsed_ms;
    }
    ++upload_profile_chunks_;

    if (fr != FR_OK || written != payload_len) {
        upload_abort_cleanup();
        char kv[64];
        std::snprintf(kv,
                      sizeof(kv),
                      "SEQ=%lu FRESULT=%d WRITTEN=%lu LEN=%lu",
                      static_cast<unsigned long>(sequence),
                      static_cast<int>(fr),
                      static_cast<unsigned long>(written),
                      static_cast<unsigned long>(payload_len));
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
        state_changed_ = true;
        return false;
    }

    const uint32_t bytes_written = ctx.bytes_written + written;
    transfer_.note_upload_chunk_committed(sequence, bytes_written, next_crc);
    ++upload_next_receive_sequence_;
    upload_bytes_received_ += payload_len;
    upload_receive_crc_running_ = next_crc;
    upload_queue_high_water_ = upload_queue_high_water_ == 0 ? 1 : upload_queue_high_water_;

    if ((sequence % 64u) == 0u || bytes_written >= ctx.expected_size) {
        const uint32_t total_ms = to_ms_since_boot(get_absolute_time()) - upload_profile_start_ms_;
        const uint32_t bps = total_ms > 0
            ? static_cast<uint32_t>((static_cast<uint64_t>(bytes_written) * 1000u) / total_ms)
            : 0u;
        char profile_kv[192];
        std::snprintf(profile_kv,
                      sizeof(profile_kv),
                      "SEQ=%lu BYTES=%lu TOTAL_MS=%lu WRITE_MS=%lu LAST_WRITE_MS=%lu MAX_WRITE_MS=%lu CHUNKS=%lu QUEUE=0 QUEUE_MAX=%lu BPS=%lu",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned long>(bytes_written),
                      static_cast<unsigned long>(total_ms),
                      static_cast<unsigned long>(upload_profile_write_ms_),
                      static_cast<unsigned long>(write_elapsed_ms),
                      static_cast<unsigned long>(upload_profile_max_write_ms_),
                      static_cast<unsigned long>(upload_profile_chunks_),
                      static_cast<unsigned long>(upload_queue_high_water_),
                      static_cast<unsigned long>(bps));
        emit_event_kv("STORAGE_UPLOAD_CHUNK_PROFILE", profile_kv);
    }

    const bool should_ack =
        ((sequence + 1u) % kUploadAckStride) == 0u ||
        bytes_written >= ctx.expected_size;
    if (should_ack) {
        send_upload_chunk_ack(sequence, bytes_written);
    }

    return true;
}

void DesktopProtocol::process_upload_results() {
    while (true) {
        UploadCommitResult result{};
        critical_section_enter_blocking(&upload_worker_lock_);
        if (upload_result_count_ == 0) {
            critical_section_exit(&upload_worker_lock_);
            return;
        }
        result = upload_results_[upload_result_head_];
        upload_result_head_ = (upload_result_head_ + 1u) % kUploadQueueCapacity;
        --upload_result_count_;
        critical_section_exit(&upload_worker_lock_);
        commit_upload_result(result);
    }
}

void DesktopProtocol::commit_upload_result(const UploadCommitResult& result) {
    const auto& ctx = transfer_.context();
    if (!transfer_.is_upload() || result.transfer_id != ctx.session_id) {
        return;
    }
    if (result.sequence != ctx.expected_sequence) {
        char kv[64];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu EXPECTED=%lu",
                      static_cast<unsigned long>(result.sequence),
                      static_cast<unsigned long>(ctx.expected_sequence));
        emit_storage_error(StorageTransferError::BadSequence, StorageTransferOperation::Upload, kv);
        return;
    }

    upload_profile_write_ms_ += result.write_elapsed_ms;
    if (result.write_elapsed_ms > upload_profile_max_write_ms_) {
        upload_profile_max_write_ms_ = result.write_elapsed_ms;
    }
    ++upload_profile_chunks_;
    if (result.result != FR_OK || result.written != result.length) {
        upload_abort_cleanup();
        char kv[32];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu", static_cast<unsigned long>(result.sequence));
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
        state_changed_ = true;
        return;
    }

    const uint32_t bytes_written = ctx.bytes_written + result.length;
    transfer_.note_upload_chunk_committed(result.sequence, bytes_written, result.crc_after);
    if ((result.sequence % 64u) == 0u || bytes_written >= ctx.expected_size) {
        const uint32_t total_ms = to_ms_since_boot(get_absolute_time()) - upload_profile_start_ms_;
        const uint32_t bps = total_ms > 0
            ? static_cast<uint32_t>((static_cast<uint64_t>(bytes_written) * 1000u) / total_ms)
            : 0u;
        size_t queue_count = 0;
        critical_section_enter_blocking(&upload_worker_lock_);
        queue_count = upload_queue_count_;
        critical_section_exit(&upload_worker_lock_);
        char profile_kv[192];
        std::snprintf(profile_kv,
                      sizeof(profile_kv),
                      "SEQ=%lu BYTES=%lu TOTAL_MS=%lu WRITE_MS=%lu LAST_WRITE_MS=%lu MAX_WRITE_MS=%lu CHUNKS=%lu QUEUE=%lu QUEUE_MAX=%lu BPS=%lu",
                      static_cast<unsigned long>(result.sequence),
                      static_cast<unsigned long>(bytes_written),
                      static_cast<unsigned long>(total_ms),
                      static_cast<unsigned long>(upload_profile_write_ms_),
                      static_cast<unsigned long>(result.write_elapsed_ms),
                      static_cast<unsigned long>(upload_profile_max_write_ms_),
                      static_cast<unsigned long>(upload_profile_chunks_),
                      static_cast<unsigned long>(queue_count),
                      static_cast<unsigned long>(upload_queue_high_water_),
                      static_cast<unsigned long>(bps));
        emit_event_kv("STORAGE_UPLOAD_CHUNK_PROFILE", profile_kv);
    }

    const bool should_ack =
        ((result.sequence + 1u) % kUploadAckStride) == 0u ||
        bytes_written >= ctx.expected_size;
    if (should_ack) {
        send_upload_chunk_ack(result.sequence, bytes_written);
    }
}

void DesktopProtocol::reset_upload_queue() {
    upload_worker_enabled_ = false;
    while (upload_worker_busy_) {
        sleep_us(50);
    }
    critical_section_enter_blocking(&upload_worker_lock_);
    upload_queue_head_ = 0;
    upload_queue_tail_ = 0;
    upload_queue_count_ = 0;
    upload_queue_high_water_ = 0;
    upload_result_head_ = 0;
    upload_result_tail_ = 0;
    upload_result_count_ = 0;
    upload_worker_busy_ = false;
    upload_next_receive_sequence_ = 0;
    upload_bytes_received_ = 0;
    upload_receive_crc_running_ = 0xFFFFFFFFu;
    critical_section_exit(&upload_worker_lock_);
}

void DesktopProtocol::storage_worker_entry() {
    if (storage_worker_instance_ != nullptr) {
        storage_worker_instance_->storage_worker_loop();
    }
}

void DesktopProtocol::storage_worker_loop() {
    while (true) {
        bool has_entry = false;

        critical_section_enter_blocking(&upload_worker_lock_);
        if (upload_worker_enabled_ && upload_queue_count_ > 0 && upload_result_count_ < kUploadQueueCapacity) {
            upload_worker_entry_ = upload_queue_[upload_queue_head_];
            upload_queue_head_ = (upload_queue_head_ + 1u) % kUploadQueueCapacity;
            --upload_queue_count_;
            upload_worker_busy_ = true;
            has_entry = true;
        }
        critical_section_exit(&upload_worker_lock_);

        if (!has_entry) {
            sleep_us(100);
            continue;
        }

        UploadCommitResult result{};
        result.transfer_id = upload_worker_entry_.transfer_id;
        result.sequence = upload_worker_entry_.sequence;
        result.length = upload_worker_entry_.length;
        result.crc_after = upload_worker_entry_.crc_after;
        const uint32_t write_start_ms = to_ms_since_boot(get_absolute_time());
        result.result = f_write(&transfer_.file(), upload_worker_entry_.payload, upload_worker_entry_.length, &result.written);
        result.write_elapsed_ms = to_ms_since_boot(get_absolute_time()) - write_start_ms;

        critical_section_enter_blocking(&upload_worker_lock_);
        if (upload_result_count_ < kUploadQueueCapacity) {
            upload_results_[upload_result_tail_] = result;
            upload_result_tail_ = (upload_result_tail_ + 1u) % kUploadQueueCapacity;
            ++upload_result_count_;
        }
        upload_worker_busy_ = false;
        critical_section_exit(&upload_worker_lock_);
    }
}

// -- Storage callbacks --------------------------------------------------------

void DesktopProtocol::on_sd_mounted() {
    inject(MachineEvent::SdMounted);
    emit_state_update();
    emit_event("SD_MOUNTED");
}

void DesktopProtocol::on_sd_removed() {
    if (transfer_.is_upload()) {
        abort_active_storage_transfer(true, true);
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Upload);
        state_changed_ = true;
    }
    if (transfer_.is_download()) {
        abort_active_storage_transfer(true, false);
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Download);
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
    return crc32_update_table(crc, data, len);
}

static uint32_t crc32_update_table(uint32_t crc, const uint8_t* data, size_t len) {
    static bool initialized = false;
    static uint32_t table[256];

    if (!initialized) {
        for (uint32_t value = 0; value < 256; value++) {
            uint32_t entry = value;
            for (int bit = 0; bit < 8; bit++) {
                entry = (entry & 1u) != 0 ? (entry >> 1) ^ 0xEDB88320u : (entry >> 1);
            }
            table[value] = entry;
        }
        initialized = true;
    }

    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}
