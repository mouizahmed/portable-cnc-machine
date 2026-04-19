#include "protocol/desktop_protocol.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

// â”€â”€ Constructor â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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

// â”€â”€ poll / dispatch â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void DesktopProtocol::poll() {
    while (transport_.poll_line(line_, sizeof(line_))) {
        dispatch(line_);
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
    else if (std::strcmp(verb, "@CHUNK")       == 0) { handle_chunk(params); }
    else if (std::strcmp(verb, "@FILE_UPLOAD_END")   == 0) { handle_file_upload_end(params); }
    else if (std::strcmp(verb, "@FILE_UPLOAD_ABORT") == 0) { handle_file_upload_abort(); }
    else if (std::strcmp(verb, "@FILE_DOWNLOAD") == 0) { handle_file_download(params); }
    else if (std::strcmp(verb, "@ACK")           == 0) { handle_download_ack(params); }
    // Unknown verbs are silently ignored
}

// â”€â”€ Outbound helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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
        transport_.send_fmt("@JOB NAME=%s", loaded->name);
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

// â”€â”€ inject helper â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void DesktopProtocol::inject(MachineEvent ev) {
    msm_.handle_event(ev);
}

// â”€â”€ Command handlers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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
    // params contains AXIS=X DIST=10.0 FEED=500 etc. â€” passed through for future use
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

        transport_.send_fmt("@FILE %s SIZE=%lu", info.fname, (unsigned long)info.fsize);
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

    const int16_t index = find_job_index_by_name(name);
    if (index < 0 || !load_job_by_index(index)) {
        emit_error("FILE_NOT_FOUND");
        return;
    }

    char kv[80];
    std::snprintf(kv, sizeof(kv), "NAME=%s", name);
    emit_ok_kv("FILE_LOAD", kv);
}

void DesktopProtocol::handle_file_unload() {
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
    if (!msm_.sd_mounted()) {
        emit_error("SD_NOT_MOUNTED");
        return;
    }

    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) {
        emit_error("MISSING_PARAM");
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
        char kv[80];
        std::snprintf(kv, sizeof(kv), "NAME=%s", name);
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

    uint32_t size      = param_get_u32(params, "SIZE", 0);
    int      overwrite = param_get_int(params, "OVERWRITE", 0);

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    // Check if file already exists
    FILINFO info;
    FRESULT stat_r = f_stat(path, &info);
    if (stat_r == FR_OK && !overwrite) {
        transport_.send_fmt("@ERROR FILE_EXISTS NAME=%s", name);
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
        transport_.send_fmt("@ERROR SD_FULL FREE=%llu", (unsigned long long)free_bytes);
        return;
    }

    // Zero the FIL struct before (re)use â€” f_open() expects a fresh object;
    // stale fields from a previous close can cause undefined behaviour in FatFS.
    upload_.file = FIL{};

    // Open file for writing
    FRESULT fr = f_open(&upload_.file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        emit_error("SD_WRITE_FAIL");
        return;
    }

    // Initialise upload state
    upload_.active        = true;
    upload_.expected_size = size;
    upload_.bytes_written = 0;
    upload_.expected_seq  = 0;
    upload_.crc_running   = 0xFFFFFFFFu;
    std::strncpy(upload_.name, name, sizeof(upload_.name) - 1);
    upload_.name[sizeof(upload_.name) - 1] = '\0';

    inject(MachineEvent::FileUploadCmd);
    emit_state_update();

    char kv[80];
    std::snprintf(kv, sizeof(kv), "NAME=%s", name);
    emit_ok_kv("FILE_UPLOAD_READY", kv);
}

void DesktopProtocol::handle_chunk(const char* params) {
    if (!upload_.active) {
        emit_error("INVALID_STATE");
        return;
    }

    int seq = param_get_int(params, "SEQ", -1);
    if (seq < 0) {
        emit_error("MISSING_PARAM");
        return;
    }

    if (seq != upload_.expected_seq) {
        transport_.send_fmt("@ERROR CHUNK SEQ=%d REASON=BAD_SEQ", seq);
        return;
    }

    chunk_b64_[0] = '\0';
    if (!param_get(params, "DATA", chunk_b64_, sizeof(chunk_b64_))) {
        transport_.send_fmt("@ERROR CHUNK SEQ=%d REASON=MISSING_DATA", seq);
        return;
    }

    // Decode base64
    size_t decoded_len = 0;
    if (!base64_decode(chunk_b64_, chunk_decoded_, &decoded_len, sizeof(chunk_decoded_))) {
        transport_.send_fmt("@ERROR CHUNK SEQ=%d REASON=BAD_BASE64", seq);
        return;
    }

    // Write to file
    UINT written = 0;
    FRESULT fr = f_write(&upload_.file, chunk_decoded_, (UINT)decoded_len, &written);
    if (fr != FR_OK || written != (UINT)decoded_len) {
        upload_abort_cleanup();
        transport_.send_fmt("@ERROR CHUNK SEQ=%d REASON=SD_WRITE_FAIL", seq);
        return;
    }

    // Update CRC and counters
    upload_.crc_running = crc32_update(upload_.crc_running, chunk_decoded_, decoded_len);
    upload_.bytes_written += (uint32_t)decoded_len;
    upload_.expected_seq++;

    transport_.send_fmt("@OK CHUNK SEQ=%d", seq);
}

void DesktopProtocol::handle_file_upload_end(const char* params) {
    if (!upload_.active) {
        emit_error("INVALID_STATE");
        return;
    }

    char crc_str[16] = {};
    param_get(params, "CRC", crc_str, sizeof(crc_str));

    // Close the file
    f_close(&upload_.file);

    uint32_t final_crc = ~upload_.crc_running;
    uint32_t expected_crc = (uint32_t)std::strtoul(crc_str, nullptr, 16);

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", upload_.name);

    if (final_crc != expected_crc) {
        f_unlink(path);
        upload_.active = false;
        inject(MachineEvent::UploadFailed);
        emit_state_update();
        emit_error("FILE_UPLOAD_CRC_FAIL");
        return;
    }

    char name_copy[64];
    uint32_t bytes_copy = upload_.bytes_written;
    std::strncpy(name_copy, upload_.name, sizeof(name_copy));
    name_copy[sizeof(name_copy) - 1] = '\0';

    upload_.active = false;
    file_list_changed_ = true;
    inject(MachineEvent::UploadComplete);
    emit_state_update();
    transport_.send_fmt("@OK FILE_UPLOAD_END NAME=%s SIZE=%lu",
                        name_copy, (unsigned long)bytes_copy);
}

void DesktopProtocol::handle_file_upload_abort() {
    upload_abort_cleanup();
    inject(MachineEvent::UploadAborted);
    emit_state_update();
    emit_ok("FILE_UPLOAD_ABORT");
}

// â”€â”€ Download handlers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void DesktopProtocol::handle_file_download(const char* params) {
    if (!is_sd_capable_state(msm_.state())) { emit_error("INVALID_STATE"); return; }
    if (!msm_.sd_mounted()) { emit_error("SD_NOT_MOUNTED"); return; }

    char name[64] = {};
    if (!param_get(params, "NAME", name, sizeof(name))) { emit_error("MISSING_PARAM"); return; }

    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", name);

    FILINFO info;
    if (f_stat(path, &info) != FR_OK) { emit_error("FILE_NOT_FOUND"); return; }

    download_.file = FIL{};
    if (f_open(&download_.file, path, FA_READ) != FR_OK) { emit_error("SD_READ_FAIL"); return; }

    download_.active      = true;
    download_.total_size  = info.fsize;
    download_.bytes_sent  = 0;
    download_.crc_running = 0xFFFFFFFFu;
    download_.next_seq    = 0;
    std::strncpy(download_.name, name, sizeof(download_.name) - 1);
    download_.name[sizeof(download_.name) - 1] = '\0';

    char kv[80];
    std::snprintf(kv, sizeof(kv), "NAME=%s SIZE=%lu", name, (unsigned long)info.fsize);
    emit_ok_kv("FILE_DOWNLOAD_READY", kv);

    send_next_download_chunk();
}

void DesktopProtocol::handle_download_ack(const char* params) {
    if (!download_.active) return;
    const int seq = param_get_int(params, "SEQ", -1);
    if (seq != download_.next_seq - 1) return;  // stale or out-of-order, ignore
    send_next_download_chunk();
}

void DesktopProtocol::send_next_download_chunk() {
    UINT bytes_read = 0;
    const FRESULT fr = f_read(&download_.file, chunk_raw_, sizeof(chunk_raw_), &bytes_read);
    if (fr != FR_OK) {
        f_close(&download_.file);
        download_.active = false;
        emit_error("SD_READ_FAIL");
        return;
    }

    if (bytes_read == 0) {
        // EOF â€” send end
        const uint32_t final_crc = ~download_.crc_running;
        f_close(&download_.file);
        download_.active = false;
        transport_.send_fmt("@OK FILE_DOWNLOAD_END NAME=%s CRC=%08lx",
                            download_.name, (unsigned long)final_crc);
        return;
    }

    download_.crc_running = crc32_update(download_.crc_running, chunk_raw_, bytes_read);
    download_.bytes_sent += bytes_read;

    base64_encode(chunk_raw_, bytes_read, chunk_encode_, sizeof(chunk_encode_));
    transport_.send_fmt("@CHUNK SEQ=%d DATA=%s", download_.next_seq, chunk_encode_);
    download_.next_seq++;
}

// â”€â”€ Upload helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void DesktopProtocol::upload_abort_cleanup() {
    if (!upload_.active) return;
    f_close(&upload_.file);
    char path[80];
    std::snprintf(path, sizeof(path), "0:/%s", upload_.name);
    f_unlink(path);
    upload_.active = false;
}

// â”€â”€ Storage callbacks â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void DesktopProtocol::on_sd_mounted() {
    inject(MachineEvent::SdMounted);
    emit_state_update();
    emit_event("SD_MOUNTED");
}

void DesktopProtocol::on_sd_removed() {
    if (upload_.active) {
        upload_abort_cleanup();
    }
    inject(MachineEvent::SdRemoved);
    jobs_.handle_event(JobEvent::ClearLoadedFile);
    emit_state_update();
    emit_job();
    emit_event("SD_REMOVED");
}

// â”€â”€ Param helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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

// â”€â”€ Encoding helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void DesktopProtocol::base64_encode(const uint8_t* in, size_t len,
                                     char* out, size_t out_cap) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        if (o + 4 >= out_cap) break;
        const uint8_t b0 = in[i];
        const uint8_t b1 = (i + 1 < len) ? in[i + 1] : 0u;
        const uint8_t b2 = (i + 2 < len) ? in[i + 2] : 0u;
        out[o++] = kTable[b0 >> 2];
        out[o++] = kTable[((b0 & 0x03) << 4) | (b1 >> 4)];
        out[o++] = (i + 1 < len) ? kTable[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=';
        out[o++] = (i + 2 < len) ? kTable[b2 & 0x3F] : '=';
    }
    if (o < out_cap) out[o] = '\0';
}

bool DesktopProtocol::base64_decode(const char* in, uint8_t* out,
                                     size_t* out_len, size_t out_cap) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    auto index_of = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    (void)table;

    size_t in_len = std::strlen(in);
    *out_len = 0;

    // Input length must be a multiple of 4
    if (in_len % 4 != 0) return false;

    for (size_t i = 0; i < in_len; i += 4) {
        int a = index_of(in[i]);
        int b = index_of(in[i + 1]);
        int c = (in[i + 2] == '=') ? 0 : index_of(in[i + 2]);
        int d = (in[i + 3] == '=') ? 0 : index_of(in[i + 3]);

        if (a < 0 || b < 0) return false;
        if (in[i + 2] != '=' && c < 0) return false;
        if (in[i + 3] != '=' && d < 0) return false;

        if (*out_len < out_cap) out[(*out_len)++] = (uint8_t)((a << 2) | (b >> 4));
        if (in[i + 2] != '=') {
            if (*out_len < out_cap) out[(*out_len)++] = (uint8_t)((b << 4) | (c >> 2));
        }
        if (in[i + 3] != '=') {
            if (*out_len < out_cap) out[(*out_len)++] = (uint8_t)((c << 6) | d);
        }
    }
    return true;
}

uint32_t DesktopProtocol::crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc;
}
