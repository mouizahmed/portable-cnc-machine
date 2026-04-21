#include "protocol/desktop_protocol.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "protocol/protocol_defs.h"
#include "pico/stdlib.h"

#ifndef PCNC_UPLOAD_READBACK_VERIFY_PASSES
#define PCNC_UPLOAD_READBACK_VERIFY_PASSES 0
#endif

static_assert(UsbCdcTransport::kMaxTransferPayloadSize <= kCore1WorkerTransferChunkBytes,
              "Core1Worker upload jobs must fit USB transfer chunks");

static void percent_decode_in_place(char* text);
static void percent_encode_value(const char* input, char* output, size_t output_size);
static uint32_t crc32_update_table(uint32_t crc, const uint8_t* data, size_t len);

namespace {

template <typename T>
bool copy_command_payload(const UsbCdcTransport::FramePacket& frame, T& out) {
    if (frame.payload_len != sizeof(T)) {
        return false;
    }
    std::memcpy(&out, frame.payload, sizeof(T));
    return true;
}

template <typename T, typename SendFn>
void send_binary_payload(SendFn&& send_fn, const T& payload) {
    send_fn(reinterpret_cast<const uint8_t*>(&payload), static_cast<uint16_t>(sizeof(T)));
}

void copy_wire_text(const char* input, size_t input_size, char* output, size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == nullptr || input_size == 0) {
        return;
    }

    size_t copy = 0;
    while (copy < input_size && input[copy] != '\0' && copy + 1 < output_size) {
        output[copy] = input[copy];
        ++copy;
    }
    output[copy] = '\0';
}

void write_wire_text(const char* input, char* output, size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == nullptr) {
        return;
    }

    std::strncpy(output, input, output_size - 1u);
    output[output_size - 1u] = '\0';
}

uint8_t command_type_from_token(const char* token) {
    if (token == nullptr) return 0;
    if (std::strcmp(token, "HOME") == 0) return CMD_HOME;
    if (std::strcmp(token, "JOG") == 0) return CMD_JOG;
    if (std::strcmp(token, "JOG_CANCEL") == 0) return CMD_JOG_CANCEL;
    if (std::strcmp(token, "ZERO") == 0) return CMD_ZERO;
    if (std::strcmp(token, "START") == 0) return CMD_START;
    if (std::strcmp(token, "PAUSE") == 0) return CMD_PAUSE;
    if (std::strcmp(token, "RESUME") == 0) return CMD_RESUME;
    if (std::strcmp(token, "ABORT") == 0) return CMD_ABORT;
    if (std::strcmp(token, "ESTOP") == 0) return CMD_ESTOP;
    if (std::strcmp(token, "RESET") == 0) return CMD_RESET;
    if (std::strcmp(token, "SPINDLE_ON") == 0) return CMD_SPINDLE_ON;
    if (std::strcmp(token, "SPINDLE_OFF") == 0) return CMD_SPINDLE_OFF;
    if (std::strcmp(token, "OVERRIDE") == 0) return CMD_OVERRIDE;
    if (std::strcmp(token, "FILE_UNLOAD") == 0) return CMD_FILE_UNLOAD;
    if (std::strcmp(token, "FILE_UPLOAD_ABORT") == 0) return CMD_FILE_UPLOAD_ABORT;
    if (std::strcmp(token, "FILE_DOWNLOAD_ABORT") == 0) return CMD_FILE_DOWNLOAD_ABORT;
    return 0;
}

uint8_t protocol_error_from_reason(const char* reason) {
    if (reason == nullptr) return ERROR_UNKNOWN;
    if (std::strcmp(reason, "INVALID_STATE") == 0) return ERROR_INVALID_STATE;
    if (std::strcmp(reason, "MISSING_PARAM") == 0) return ERROR_MISSING_PARAM;
    if (std::strcmp(reason, "NO_JOB_LOADED") == 0) return ERROR_NO_JOB_LOADED;
    if (std::strcmp(reason, "UPLOAD_FILE_EXISTS") == 0) return ERROR_UPLOAD_FILE_EXISTS;
    if (std::strcmp(reason, "DOWNLOAD_MISSING_PARAM") == 0) return ERROR_DOWNLOAD_MISSING_PARAM;
    if (std::strcmp(reason, "UPLOAD_MISSING_PARAM") == 0) return ERROR_UPLOAD_MISSING_PARAM;
    return ERROR_UNKNOWN;
}

uint8_t protocol_state(MachineOperationState state) {
    switch (state) {
        case MachineOperationState::Booting:            return MACHINE_BOOTING;
        case MachineOperationState::Syncing:            return MACHINE_SYNCING;
        case MachineOperationState::TeensyDisconnected: return MACHINE_TEENSY_DISCONNECTED;
        case MachineOperationState::Idle:               return MACHINE_IDLE;
        case MachineOperationState::Homing:             return MACHINE_HOMING;
        case MachineOperationState::Jog:                return MACHINE_JOG;
        case MachineOperationState::Starting:           return MACHINE_STARTING;
        case MachineOperationState::Running:            return MACHINE_RUNNING;
        case MachineOperationState::Hold:               return MACHINE_HOLD;
        case MachineOperationState::Fault:              return MACHINE_FAULT;
        case MachineOperationState::Estop:              return MACHINE_ESTOP;
        case MachineOperationState::CommsFault:         return MACHINE_COMMS_FAULT;
        case MachineOperationState::Uploading:          return MACHINE_UPLOADING;
    }
    return MACHINE_BOOTING;
}

uint8_t protocol_safety(SafetyLevel safety) {
    switch (safety) {
        case SafetyLevel::Safe:       return SAFETY_SAFE;
        case SafetyLevel::Monitoring: return SAFETY_MONITORING;
        case SafetyLevel::Warning:    return SAFETY_WARNING;
        case SafetyLevel::Critical:   return SAFETY_CRITICAL;
    }
    return SAFETY_SAFE;
}

uint16_t protocol_caps(CapsFlags caps) {
    uint16_t value = 0;
    if (caps.motion)     value |= CAP_MOTION;
    if (caps.probe)      value |= CAP_PROBE;
    if (caps.spindle)    value |= CAP_SPINDLE;
    if (caps.file_load)  value |= CAP_FILE_LOAD;
    if (caps.job_start)  value |= CAP_JOB_START;
    if (caps.job_pause)  value |= CAP_JOB_PAUSE;
    if (caps.job_resume) value |= CAP_JOB_RESUME;
    if (caps.job_abort)  value |= CAP_JOB_ABORT;
    if (caps.overrides)  value |= CAP_OVERRIDES;
    if (caps.reset)      value |= CAP_RESET;
    return value;
}

uint8_t protocol_storage_operation(StorageTransferOperation operation) {
    switch (operation) {
        case StorageTransferOperation::None:     return STORAGE_OP_NONE;
        case StorageTransferOperation::List:     return STORAGE_OP_LIST;
        case StorageTransferOperation::Load:     return STORAGE_OP_LOAD;
        case StorageTransferOperation::Unload:   return STORAGE_OP_UNLOAD;
        case StorageTransferOperation::Delete:   return STORAGE_OP_DELETE;
        case StorageTransferOperation::Upload:   return STORAGE_OP_UPLOAD;
        case StorageTransferOperation::Download: return STORAGE_OP_DOWNLOAD;
    }
    return STORAGE_OP_NONE;
}

uint8_t protocol_storage_error(StorageTransferError error) {
    switch (error) {
        case StorageTransferError::Busy:            return ERROR_STORAGE_BUSY;
        case StorageTransferError::NotAllowed:      return ERROR_STORAGE_NOT_ALLOWED;
        case StorageTransferError::SdNotMounted:    return ERROR_STORAGE_NO_SD;
        case StorageTransferError::FileNotFound:    return ERROR_STORAGE_FILE_NOT_FOUND;
        case StorageTransferError::InvalidFilename: return ERROR_STORAGE_INVALID_FILENAME;
        case StorageTransferError::InvalidSession:  return ERROR_STORAGE_INVALID_SESSION;
        case StorageTransferError::BadSequence:     return ERROR_STORAGE_BAD_SEQUENCE;
        case StorageTransferError::SizeMismatch:    return ERROR_STORAGE_SIZE_MISMATCH;
        case StorageTransferError::CrcMismatch:     return ERROR_STORAGE_CRC_FAIL;
        case StorageTransferError::ReadFail:        return ERROR_STORAGE_READ_FAIL;
        case StorageTransferError::WriteFail:       return ERROR_STORAGE_WRITE_FAIL;
        case StorageTransferError::NoSpace:         return ERROR_STORAGE_NO_SPACE;
        case StorageTransferError::Aborted:         return ERROR_STORAGE_ABORTED;
        case StorageTransferError::None:            return ERROR_NONE;
    }
    return ERROR_UNKNOWN;
}

struct CommandContextScope {
    bool& handling;
    uint32_t& request_seq;
    uint8_t& command_type;
    bool previous_handling;
    uint32_t previous_request_seq;
    uint8_t previous_command_type;

    CommandContextScope(bool& handling_ref,
                        uint32_t& request_seq_ref,
                        uint8_t& command_type_ref,
                        uint32_t next_request_seq,
                        uint8_t next_command_type)
        : handling(handling_ref),
          request_seq(request_seq_ref),
          command_type(command_type_ref),
          previous_handling(handling_ref),
          previous_request_seq(request_seq_ref),
          previous_command_type(command_type_ref) {
        handling = true;
        request_seq = next_request_seq;
        command_type = next_command_type;
    }

    ~CommandContextScope() {
        handling = previous_handling;
        request_seq = previous_request_seq;
        command_type = previous_command_type;
    }
};

const char* axis_name(uint8_t axis) {
    switch (axis) {
        case AXIS_X: return "X";
        case AXIS_Y: return "Y";
        case AXIS_Z: return "Z";
        default:     return nullptr;
    }
}

const char* axes_name(uint8_t axes_mask) {
    switch (axes_mask) {
        case AXES_X:   return "X";
        case AXES_Y:   return "Y";
        case AXES_Z:   return "Z";
        case AXES_ALL: return "ALL";
        default:       return nullptr;
    }
}

const char* override_name(uint8_t target) {
    switch (target) {
        case OVERRIDE_FEED:    return "FEED";
        case OVERRIDE_SPINDLE: return "SPINDLE";
        case OVERRIDE_RAPID:   return "RAPID";
        default:               return nullptr;
    }
}

bool suppress_background_worker_jobs(Core1Worker& worker, uint32_t timeout_ms = 250u) {
    worker.clear_background_jobs();

    const uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        const Core1WorkerSnapshot snapshot = worker.snapshot();
        if (!snapshot.busy || snapshot.active_intent != Core1JobIntent::BackgroundDisposable) {
            return true;
        }

        if ((to_ms_since_boot(get_absolute_time()) - start_ms) >= timeout_ms) {
            return false;
        }

        sleep_us(100);
    }
}

} // namespace

// -- Constructor --------------------------------------------------------------

DesktopProtocol::DesktopProtocol(UsbCdcTransport& transport,
                                 MachineFsm& msm,
                                 JogStateMachine& jogs,
                                 JobStateMachine& jobs,
                                 LoadedJobStorage& loaded_job_storage,
                                 MachineSettingsStore& machine_settings,
                                 StorageService& storage,
                                 SdSpiCard& sd,
                                 OperationCoordinator& coordinator,
                                 Core1Worker& worker)
    : transport_(transport),
      msm_(msm),
      jogs_(jogs),
      jobs_(jobs),
      loaded_job_storage_(loaded_job_storage),
      machine_settings_(machine_settings),
      storage_(storage),
      sd_(sd),
      coordinator_(coordinator),
      worker_(worker) {}

bool DesktopProtocol::admit_operation(OperationRequestType type,
                                      StorageTransferOperation operation,
                                      bool storage_error_response) {
    const RequestDecision decision = coordinator_.decide(
        OperationRequest{type, OperationRequestSource::Desktop},
        msm_,
        transfer_,
        JobStreamState::Idle,
        worker_.snapshot(),
        storage_.state());

    if (decision.type == RequestDecisionType::AcceptNow ||
        decision.type == RequestDecisionType::PreemptAndAccept ||
        decision.type == RequestDecisionType::AbortCurrentAndAccept ||
        decision.type == RequestDecisionType::SuppressBackgroundAndAccept) {
        if (decision.type == RequestDecisionType::SuppressBackgroundAndAccept &&
            !suppress_background_worker_jobs(worker_)) {
            if (storage_error_response) {
                emit_storage_error(StorageTransferError::Busy, operation);
            } else {
                emit_error("BUSY");
            }
            return false;
        }
        return true;
    }

    if (storage_error_response) {
        if ((type == OperationRequestType::FileList ||
             type == OperationRequestType::FileLoad ||
             type == OperationRequestType::FileDelete ||
             type == OperationRequestType::UploadBegin ||
             type == OperationRequestType::DownloadBegin) &&
            (storage_.state() != StorageState::Mounted || !msm_.sd_mounted())) {
            emit_storage_error(StorageTransferError::SdNotMounted, operation);
            return false;
        }

        emit_storage_error(decision.type == RequestDecisionType::RejectBusy
                               ? StorageTransferError::Busy
                               : StorageTransferError::NotAllowed,
                           operation);
        return false;
    }

    emit_error(decision.type == RequestDecisionType::RejectBusy ? "BUSY" : "INVALID_STATE");
    return false;
}

bool DesktopProtocol::allow_ui_operation(OperationRequestType type) {
    const RequestDecision decision = coordinator_.decide(
        OperationRequest{type, OperationRequestSource::Tft},
        msm_,
        transfer_,
        JobStreamState::Idle,
        worker_.snapshot(),
        storage_.state());

    return decision.type == RequestDecisionType::AcceptNow ||
           decision.type == RequestDecisionType::PreemptAndAccept ||
           decision.type == RequestDecisionType::AbortCurrentAndAccept ||
           decision.type == RequestDecisionType::SuppressBackgroundAndAccept;
}

uint32_t DesktopProtocol::response_request_seq() const {
    return handling_command_ ? current_request_seq_ : storage_request_seq_;
}

// -- Poll / dispatch ----------------------------------------------------------

void DesktopProtocol::poll() {
    while (true) {
        const auto kind = transport_.poll(line_, sizeof(line_), frame_);
        if (kind == UsbCdcTransport::PacketKind::None) {
            tick_transfer_retries();
            process_upload_results();
            service_pending_file_list_request();
            return;
        }
        if (kind == UsbCdcTransport::PacketKind::Frame) {
            dispatch_frame(frame_);
        }
    }
}

void DesktopProtocol::dispatch_command_frame(const UsbCdcTransport::FramePacket& frame) {
    if (frame.transfer_id != PCNC_TRANSFER_ID_NONE || frame.payload_len == 0) {
        emit_error("MALFORMED_CMD");
        return;
    }

    const uint8_t message_type = frame.payload[0];
    CommandContextScope command_context(handling_command_,
                                        current_request_seq_,
                                        current_command_type_,
                                        frame.seq,
                                        message_type);
    switch (message_type) {
        case CMD_PING: {
            CmdPing cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_ping();
            break;
        }
        case CMD_INFO: {
            CmdInfo cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_info();
            break;
        }
        case CMD_STATUS: {
            CmdStatus cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_status();
            break;
        }
        case CMD_HOME: {
            CmdHome cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_home();
            break;
        }
        case CMD_JOG: {
            CmdJog cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            const char* axis = axis_name(cmd.axis);
            if (axis == nullptr) { emit_error("MALFORMED_CMD"); return; }
            char params[64];
            std::snprintf(params, sizeof(params), "AXIS=%s DIST=%.3f FEED=%u",
                          axis,
                          static_cast<double>(cmd.dist),
                          static_cast<unsigned int>(cmd.feed));
            handle_jog(params);
            break;
        }
        case CMD_JOG_CANCEL: {
            CmdJogCancel cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_jog_cancel();
            break;
        }
        case CMD_ZERO: {
            CmdZero cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            const char* axes = axes_name(cmd.axes_mask);
            if (axes == nullptr) { emit_error("MALFORMED_CMD"); return; }
            char params[16];
            std::snprintf(params, sizeof(params), "AXIS=%s", axes);
            handle_zero(params);
            break;
        }
        case CMD_START: {
            CmdStart cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_start();
            break;
        }
        case CMD_PAUSE: {
            CmdPause cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_pause();
            break;
        }
        case CMD_RESUME: {
            CmdResume cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_resume();
            break;
        }
        case CMD_ABORT: {
            CmdAbort cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_abort();
            break;
        }
        case CMD_ESTOP: {
            CmdEstop cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_estop();
            break;
        }
        case CMD_RESET: {
            CmdReset cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_reset();
            break;
        }
        case CMD_SPINDLE_ON: {
            CmdSpindleOn cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char params[16];
            std::snprintf(params, sizeof(params), "RPM=%u", static_cast<unsigned int>(cmd.rpm));
            handle_spindle_on(params);
            break;
        }
        case CMD_SPINDLE_OFF: {
            CmdSpindleOff cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_spindle_off();
            break;
        }
        case CMD_OVERRIDE: {
            CmdOverride cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            const char* target = override_name(cmd.target);
            if (target == nullptr) { emit_error("MALFORMED_CMD"); return; }
            char params[32];
            std::snprintf(params, sizeof(params), "%s=%u",
                          target,
                          static_cast<unsigned int>(cmd.percent));
            handle_override(params);
            break;
        }
        case CMD_FILE_LIST: {
            CmdFileList cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_file_list();
            break;
        }
        case CMD_FILE_LOAD: {
            CmdFileLoad cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char name[PCNC_MAX_FILENAME_BYTES + 1u]{};
            char encoded_name[192]{};
            char params[224]{};
            copy_wire_text(cmd.name, PCNC_MAX_FILENAME_BYTES, name, sizeof(name));
            percent_encode_value(name, encoded_name, sizeof(encoded_name));
            std::snprintf(params, sizeof(params), "NAME=%s", encoded_name);
            handle_file_load(params);
            break;
        }
        case CMD_FILE_UNLOAD: {
            CmdFileUnload cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_file_unload();
            break;
        }
        case CMD_FILE_DELETE: {
            CmdFileDelete cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char name[PCNC_MAX_FILENAME_BYTES + 1u]{};
            char encoded_name[192]{};
            char params[224]{};
            copy_wire_text(cmd.name, PCNC_MAX_FILENAME_BYTES, name, sizeof(name));
            percent_encode_value(name, encoded_name, sizeof(encoded_name));
            std::snprintf(params, sizeof(params), "NAME=%s", encoded_name);
            handle_file_delete(params);
            break;
        }
        case CMD_FILE_UPLOAD: {
            CmdFileUpload cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char name[PCNC_MAX_FILENAME_BYTES + 1u]{};
            char encoded_name[192]{};
            char params[256]{};
            copy_wire_text(cmd.name, PCNC_MAX_FILENAME_BYTES, name, sizeof(name));
            percent_encode_value(name, encoded_name, sizeof(encoded_name));
            std::snprintf(params, sizeof(params), "NAME=%s SIZE=%lu OVERWRITE=%u",
                          encoded_name,
                          static_cast<unsigned long>(cmd.size),
                          static_cast<unsigned int>(cmd.overwrite != 0 ? 1 : 0));
            handle_file_upload(params);
            break;
        }
        case CMD_FILE_UPLOAD_END: {
            CmdFileUploadEnd cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char params[16];
            std::snprintf(params, sizeof(params), "CRC=%08lx", static_cast<unsigned long>(cmd.crc32));
            handle_file_upload_end(params);
            break;
        }
        case CMD_FILE_UPLOAD_ABORT: {
            CmdFileUploadAbort cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_file_upload_abort();
            break;
        }
        case CMD_FILE_DOWNLOAD: {
            CmdFileDownload cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char name[PCNC_MAX_FILENAME_BYTES + 1u]{};
            char encoded_name[192]{};
            char params[224]{};
            copy_wire_text(cmd.name, PCNC_MAX_FILENAME_BYTES, name, sizeof(name));
            percent_encode_value(name, encoded_name, sizeof(encoded_name));
            std::snprintf(params, sizeof(params), "NAME=%s", encoded_name);
            handle_file_download(params);
            break;
        }
        case CMD_FILE_DOWNLOAD_ACK: {
            CmdFileDownloadAck cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            char params[32];
            std::snprintf(params, sizeof(params), "ID=%u SEQ=%lu",
                          static_cast<unsigned int>(cmd.transfer_id),
                          static_cast<unsigned long>(cmd.seq));
            handle_file_download_ack(params);
            break;
        }
        case CMD_FILE_DOWNLOAD_ABORT: {
            CmdFileDownloadAbort cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_file_download_abort();
            break;
        }
        case CMD_SETTINGS_GET: {
            CmdSettingsGet cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_settings_get();
            break;
        }
        case CMD_SETTINGS_SET: {
            CmdSettingsSet cmd{};
            if (!copy_command_payload(frame, cmd)) { emit_error("MALFORMED_CMD"); return; }
            handle_settings_set(cmd);
            break;
        }
        case CMD_PROBE_Z:
        case CMD_BEGIN_JOB:
        case CMD_END_JOB:
        case CMD_CLEAR_JOB:
            // Compatibility placeholders: the current text parser has no live
            // handlers for these commands, so binary frames keep the same behavior.
            break;
        default:
            break;
    }
}

void DesktopProtocol::dispatch_frame(const UsbCdcTransport::FramePacket& frame) {
    if (frame.type == kCommandFrameType) {
        dispatch_command_frame(frame);
        return;
    }

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
    const uint8_t state = protocol_state(msm_.state());
    if (handling_command_) {
        RespState payload{RESP_STATE, current_request_seq_, state};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    EventState payload{EVENT_STATE, state};
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
    }, payload);
}

void DesktopProtocol::emit_caps() {
    const uint16_t caps = protocol_caps(msm_.caps());
    if (handling_command_) {
        RespCaps payload{RESP_CAPS, current_request_seq_, caps};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    EventCaps payload{EVENT_CAPS, caps};
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
    }, payload);
}

void DesktopProtocol::emit_safety() {
    const uint8_t safety = protocol_safety(msm_.safety());
    if (handling_command_) {
        RespSafety payload{RESP_SAFETY, current_request_seq_, safety};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    EventSafety payload{EVENT_SAFETY, safety};
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
    }, payload);
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
    const FileEntry* loaded = jobs_.loaded_entry();
    if (handling_command_) {
        RespJob payload{};
        payload.message_type = RESP_JOB;
        payload.request_seq = current_request_seq_;
        payload.has_job = loaded != nullptr ? 1 : 0;
        if (loaded != nullptr) {
            write_wire_text(loaded->name, payload.name, sizeof(payload.name));
        }
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    EventJob payload{};
    payload.message_type = EVENT_JOB;
    payload.has_job = loaded != nullptr ? 1 : 0;
    if (loaded != nullptr) {
        write_wire_text(loaded->name, payload.name, sizeof(payload.name));
    }
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
    }, payload);
}

void DesktopProtocol::emit_machine_settings() {
    const MachineSettings& current = machine_settings_.current();

    RespMachineSettings payload{};
    payload.message_type = RESP_MACHINE_SETTINGS;
    payload.request_seq = handling_command_ ? current_request_seq_ : 0;
    payload.steps_per_mm_x = current.steps_per_mm_x;
    payload.steps_per_mm_y = current.steps_per_mm_y;
    payload.steps_per_mm_z = current.steps_per_mm_z;
    payload.max_feed_rate_x = current.max_feed_rate_x;
    payload.max_feed_rate_y = current.max_feed_rate_y;
    payload.max_feed_rate_z = current.max_feed_rate_z;
    payload.acceleration_x = current.acceleration_x;
    payload.acceleration_y = current.acceleration_y;
    payload.acceleration_z = current.acceleration_z;
    payload.max_travel_x = current.max_travel_x;
    payload.max_travel_y = current.max_travel_y;
    payload.max_travel_z = current.max_travel_z;
    payload.soft_limits_enabled = current.soft_limits_enabled ? 1 : 0;
    payload.hard_limits_enabled = current.hard_limits_enabled ? 1 : 0;
    payload.spindle_min_rpm = current.spindle_min_rpm;
    payload.spindle_max_rpm = current.spindle_max_rpm;
    payload.warning_temperature = current.warning_temperature;
    payload.max_temperature = current.max_temperature;
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType,
                              PCNC_TRANSFER_ID_NONE,
                              payload.request_seq,
                              data,
                              len);
    }, payload);
}

void DesktopProtocol::emit_position() {
    const float x = jogs_.x();
    const float y = jogs_.y();
    const float z = jogs_.z();
    if (handling_command_) {
        RespPos payload{RESP_POS, current_request_seq_, x, y, z, x, y, z};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    EventPos payload{EVENT_POS, x, y, z, x, y, z};
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
    }, payload);
}

void DesktopProtocol::emit_event(const char* name) {
    emit_event_kv(name, nullptr);
}

void DesktopProtocol::emit_event_kv(const char* name, const char* kv) {
    if (name == nullptr) {
        return;
    }

    if (std::strcmp(name, "JOB_COMPLETE") == 0) {
        EventJobComplete payload{EVENT_JOB_COMPLETE};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "JOB_ERROR") == 0) {
        EventJobError payload{};
        payload.message_type = EVENT_JOB_ERROR;
        char reason[PCNC_MAX_REASON_BYTES]{};
        if (kv != nullptr) {
            param_get(kv, "REASON", reason, sizeof(reason));
        }
        write_wire_text(reason, payload.reason, sizeof(payload.reason));
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "SD_MOUNTED") == 0) {
        EventSdMounted payload{EVENT_SD_MOUNTED};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "SD_REMOVED") == 0) {
        EventSdRemoved payload{EVENT_SD_REMOVED};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "TEENSY_CONNECTED") == 0) {
        EventTeensyConnected payload{EVENT_TEENSY_CONNECTED};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "TEENSY_DISCONNECTED") == 0) {
        EventTeensyDisconnected payload{EVENT_TEENSY_DISCONNECTED};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "ESTOP_ACTIVE") == 0) {
        EventEstopActive payload{EVENT_ESTOP_ACTIVE};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "ESTOP_CLEARED") == 0) {
        EventEstopCleared payload{EVENT_ESTOP_CLEARED};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "LIMIT") == 0) {
        EventLimit payload{};
        payload.message_type = EVENT_LIMIT;
        char axis[8]{};
        if (kv != nullptr && param_get(kv, "AXIS", axis, sizeof(axis))) {
            if (std::strchr(axis, 'X') != nullptr) payload.axes_mask |= AXES_X;
            if (std::strchr(axis, 'Y') != nullptr) payload.axes_mask |= AXES_Y;
            if (std::strchr(axis, 'Z') != nullptr) payload.axes_mask |= AXES_Z;
        }
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "STORAGE_UPLOAD_PROFILE") == 0) {
        EventStorageUploadProfile payload{};
        payload.message_type = EVENT_STORAGE_UPLOAD_PROFILE;
        payload.size = param_get_u32(kv, "SIZE", 0);
        payload.total_ms = param_get_u32(kv, "TOTAL_MS", 0);
        payload.prealloc_ms = param_get_u32(kv, "PREALLOC_MS", 0);
        payload.write_ms = param_get_u32(kv, "WRITE_MS", 0);
        payload.max_write_ms = param_get_u32(kv, "MAX_WRITE_MS", 0);
        payload.close_ms = param_get_u32(kv, "CLOSE_MS", 0);
        payload.chunks = param_get_u32(kv, "CHUNKS", 0);
        payload.queue_max = param_get_u32(kv, "QUEUE_MAX", 0);
        payload.bps = param_get_u32(kv, "BPS", 0);
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(name, "STORAGE_UPLOAD_CHUNK_PROFILE") == 0) {
        EventStorageUploadChunkProfile payload{};
        payload.message_type = EVENT_STORAGE_UPLOAD_CHUNK_PROFILE;
        payload.seq = param_get_u32(kv, "SEQ", 0);
        payload.bytes = param_get_u32(kv, "BYTES", 0);
        payload.total_ms = param_get_u32(kv, "TOTAL_MS", 0);
        payload.write_ms = param_get_u32(kv, "WRITE_MS", 0);
        payload.last_write_ms = param_get_u32(kv, "LAST_WRITE_MS", 0);
        payload.max_write_ms = param_get_u32(kv, "MAX_WRITE_MS", 0);
        payload.chunks = param_get_u32(kv, "CHUNKS", 0);
        payload.queue = param_get_u32(kv, "QUEUE", 0);
        payload.queue_max = param_get_u32(kv, "QUEUE_MAX", 0);
        payload.bps = param_get_u32(kv, "BPS", 0);
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kEventFrameType, PCNC_TRANSFER_ID_NONE, 0, data, len);
        }, payload);
    }
}

void DesktopProtocol::emit_ok(const char* token) {
    if (token != nullptr && std::strcmp(token, "PONG") == 0) {
        RespPong payload{RESP_PONG, current_request_seq_};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }
    if (token != nullptr && std::strcmp(token, "FILE_UNLOAD") == 0) {
        RespFileUnload payload{RESP_FILE_UNLOAD, current_request_seq_};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }
    if (token != nullptr && std::strcmp(token, "FILE_UPLOAD_ABORT") == 0) {
        RespFileUploadAbort payload{RESP_FILE_UPLOAD_ABORT, current_request_seq_};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }
    if (token != nullptr && std::strcmp(token, "FILE_DOWNLOAD_ABORT") == 0) {
        RespFileDownloadAbort payload{RESP_FILE_DOWNLOAD_ABORT, current_request_seq_};
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    uint8_t command_type = current_command_type_;
    if (command_type == 0) {
        command_type = command_type_from_token(token);
    }
    RespCommandAck payload{RESP_COMMAND_ACK, current_request_seq_, command_type};
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
    }, payload);
}

void DesktopProtocol::emit_ok_kv(const char* token, const char* kv) {
    if (kv == nullptr || kv[0] == '\0') {
        emit_ok(token);
        return;
    }

    if (std::strcmp(token, "FILE_LOAD") == 0) {
        RespFileLoad payload{};
        payload.message_type = RESP_FILE_LOAD;
        payload.request_seq = current_request_seq_;
        char name[PCNC_MAX_FILENAME_BYTES]{};
        param_get(kv, "NAME", name, sizeof(name));
        write_wire_text(name, payload.name, sizeof(payload.name));
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }
    if (std::strcmp(token, "FILE_DELETE") == 0) {
        RespFileDelete payload{};
        payload.message_type = RESP_FILE_DELETE;
        payload.request_seq = current_request_seq_;
        char name[PCNC_MAX_FILENAME_BYTES]{};
        param_get(kv, "NAME", name, sizeof(name));
        write_wire_text(name, payload.name, sizeof(payload.name));
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
        }, payload);
        return;
    }

    emit_ok(token);
}

void DesktopProtocol::emit_error(const char* reason) {
    RespError payload{};
    payload.message_type = RESP_ERROR;
    payload.request_seq = current_request_seq_;
    payload.error = protocol_error_from_reason(reason);
    write_wire_text(reason, payload.reason, sizeof(payload.reason));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
    }, payload);
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

    RespStorageError payload{};
    payload.message_type = RESP_STORAGE_ERROR;
    payload.request_seq = response_request_seq();
    payload.error = protocol_storage_error(error);
    payload.operation = protocol_storage_operation(operation);
    payload.seq = param_get_u32(kv, "SEQ", 0);
    payload.expected = param_get_u32(kv, "EXPECTED", 0);
    payload.actual = param_get_u32(kv, "ACTUAL", 0);
    write_wire_text(kv != nullptr && kv[0] != '\0' ? kv : reason,
                    payload.detail,
                    sizeof(payload.detail));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, response_request_seq(), data, len);
    }, payload);
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
    if (operation == StorageTransferOperation::Upload) {
        reset_upload_queue();
        submit_upload_abort_job(ctx.session_id, ctx.filename, delete_partial_upload_file);
    }

    if (operation == StorageTransferOperation::Download) {
        worker_.clear_pending_jobs();
        download_read_jobs_pending_ = 0;
        if (close_file) {
            submit_upload_abort_job(ctx.session_id, nullptr, false);
        }
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
    RespFileUploadReady payload{};
    payload.message_type = RESP_FILE_UPLOAD_READY;
    payload.request_seq = response_request_seq();
    payload.transfer_id = ctx.session_id;
    payload.size = ctx.expected_size;
    payload.chunk_size = static_cast<uint16_t>(kTransferRawChunkSize);
    write_wire_text(ctx.filename, payload.name, sizeof(payload.name));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, response_request_seq(), data, len);
    }, payload);
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
    RespFileUploadEnd payload{};
    payload.message_type = RESP_FILE_UPLOAD_END;
    payload.request_seq = response_request_seq();
    payload.transfer_id = ctx.completion_session_id;
    payload.size = ctx.completion_size;
    write_wire_text(ctx.completion_name, payload.name, sizeof(payload.name));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, response_request_seq(), data, len);
    }, payload);
}

void DesktopProtocol::send_download_ready() {
    const auto& ctx = transfer_.context();
    RespFileDownloadReady payload{};
    payload.message_type = RESP_FILE_DOWNLOAD_READY;
    payload.request_seq = response_request_seq();
    payload.transfer_id = ctx.session_id;
    payload.size = ctx.expected_size;
    payload.chunk_size = static_cast<uint16_t>(kTransferRawChunkSize);
    write_wire_text(ctx.filename, payload.name, sizeof(payload.name));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, response_request_seq(), data, len);
    }, payload);
}

void DesktopProtocol::send_download_complete() {
    const auto& ctx = transfer_.context();
    RespFileDownloadEnd payload{};
    payload.message_type = RESP_FILE_DOWNLOAD_END;
    payload.request_seq = response_request_seq();
    payload.transfer_id = ctx.completion_session_id;
    payload.crc32 = ctx.completion_crc;
    write_wire_text(ctx.completion_name, payload.name, sizeof(payload.name));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, response_request_seq(), data, len);
    }, payload);
}

void DesktopProtocol::fill_download_window() {
    while (transfer_.is_download() &&
           transfer_.state() == StorageTransferState::Downloading) {
        if ((transfer_.chunks_in_flight() + download_read_jobs_pending_) >= kDownloadWindowSize) break;
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
    RespInfo payload{};
    payload.message_type = RESP_INFO;
    payload.request_seq = current_request_seq_;
    write_wire_text("0.1.0", payload.firmware, sizeof(payload.firmware));
    write_wire_text("PICO2W", payload.board, sizeof(payload.board));
    payload.teensy_connected = msm_.teensy_connected() ? 1 : 0;
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, current_request_seq_, data, len);
    }, payload);
    // Push current state + caps immediately after INFO so the desktop always has
    // an up-to-date view on connect.
    emit_state_update(false);
    emit_job();
}

void DesktopProtocol::handle_status() {
    emit_state_update(false);
    emit_position();
    emit_job();
}

void DesktopProtocol::handle_settings_get() {
    emit_machine_settings();
}

void DesktopProtocol::handle_settings_set(const CmdSettingsSet& cmd) {
    if (!admit_operation(OperationRequestType::SettingsSave, StorageTransferOperation::None, false)) {
        return;
    }

    MachineSettings candidate{
        cmd.steps_per_mm_x,
        cmd.steps_per_mm_y,
        cmd.steps_per_mm_z,
        cmd.max_feed_rate_x,
        cmd.max_feed_rate_y,
        cmd.max_feed_rate_z,
        cmd.acceleration_x,
        cmd.acceleration_y,
        cmd.acceleration_z,
        cmd.max_travel_x,
        cmd.max_travel_y,
        cmd.max_travel_z,
        cmd.soft_limits_enabled != 0,
        cmd.hard_limits_enabled != 0,
        cmd.spindle_min_rpm,
        cmd.spindle_max_rpm,
        cmd.warning_temperature,
        cmd.max_temperature
    };

    const char* error_reason = nullptr;
    if (!machine_settings_.apply(candidate, &error_reason)) {
        emit_error(error_reason != nullptr ? error_reason : "SETTINGS_ERR");
        return;
    }

    emit_ok("SETTINGS_SET");
}

void DesktopProtocol::handle_home() {
    if (!admit_operation(OperationRequestType::HomeAll, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::Jog, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::JogCancel, StorageTransferOperation::None, false)) {
        return;
    }

    inject(MachineEvent::JogStop);
    emit_ok("JOG_CANCEL");

    // Stub: immediately return to IDLE
    inject(MachineEvent::GrblIdle);
    emit_state_update();
}

void DesktopProtocol::handle_zero(const char* params) {
    if (!admit_operation(OperationRequestType::ZeroAll, StorageTransferOperation::None, false)) {
        return;
    }

    (void)params;
    emit_ok("ZERO");
}

void DesktopProtocol::handle_start() {
    if (!admit_operation(OperationRequestType::JobStart, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::JobHold, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::JobResume, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::JobAbort, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::Estop, StorageTransferOperation::None, false)) {
        return;
    }

    // Simulate E-stop assertion in stub mode
    inject(MachineEvent::HwEstopAsserted);
    emit_ok("ESTOP");
    emit_state_update();
}

void DesktopProtocol::handle_reset() {
    if (!admit_operation(OperationRequestType::Reset, StorageTransferOperation::None, false)) {
        return;
    }

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
    if (pending_file_list_request_) {
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::List);
        return;
    }

    const RequestDecision decision = coordinator_.decide(
        OperationRequest{OperationRequestType::FileList, OperationRequestSource::Desktop},
        msm_,
        transfer_,
        JobStreamState::Idle,
        worker_.snapshot(),
        storage_.state());

    if (decision.type == RequestDecisionType::AcceptNow ||
        decision.type == RequestDecisionType::PreemptAndAccept ||
        decision.type == RequestDecisionType::AbortCurrentAndAccept ||
        decision.type == RequestDecisionType::SuppressBackgroundAndAccept) {
        if (decision.type == RequestDecisionType::SuppressBackgroundAndAccept &&
            !suppress_background_worker_jobs(worker_)) {
            emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::List);
            return;
        }
        if (!begin_file_list(current_request_seq_)) {
            emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::List);
        }
        return;
    }

    if (decision.type == RequestDecisionType::RejectBusy && !transfer_.is_active()) {
        pending_file_list_request_ = true;
        pending_file_list_request_seq_ = current_request_seq_;
        storage_request_seq_ = current_request_seq_;
        return;
    }

    if (storage_.state() != StorageState::Mounted || !msm_.sd_mounted()) {
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::List);
        return;
    }

    emit_storage_error(decision.type == RequestDecisionType::RejectBusy
                           ? StorageTransferError::Busy
                           : StorageTransferError::NotAllowed,
                       StorageTransferOperation::List);
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
    if (!admit_operation(OperationRequestType::FileLoad, StorageTransferOperation::Load, true)) {
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
    if (!admit_operation(OperationRequestType::FileUnload, StorageTransferOperation::Unload, true)) {
        return;
    }

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
    if (!admit_operation(OperationRequestType::FileDelete, StorageTransferOperation::Delete, true)) {
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

    storage_request_seq_ = current_request_seq_;

    Core1Job job{};
    job.type = Core1JobType::StorageDeleteFile;
    std::strncpy(job.delete_file.filename, name, sizeof(job.delete_file.filename) - 1u);
    if (!worker_.submit_control(job)) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::Delete);
    }
}

void DesktopProtocol::handle_file_upload(const char* params) {
    if (transfer_active()) {
        char name[64] = {};
        const uint32_t size = param_get_u32(params, "SIZE", 0);
        if (transfer_.is_upload() &&
            param_get(params, "NAME", name, sizeof(name)) &&
            std::strcmp(name, transfer_.filename()) == 0 &&
            size == transfer_.expected_size() &&
            upload_worker_enabled_) {
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
    if (!admit_operation(OperationRequestType::UploadBegin, StorageTransferOperation::Upload, true)) {
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
    storage_request_seq_ = current_request_seq_;
    next_transfer_id_++;
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Upload);
        return;
    }

    int      overwrite = param_get_int(params, "OVERWRITE", 0);

    reset_upload_completion();
    transfer_.set_crc_running(0xFFFFFFFFu);
    upload_profile_start_ms_ = to_ms_since_boot(get_absolute_time());
    upload_profile_prealloc_ms_ = 0;
    upload_profile_write_ms_ = 0;
    upload_profile_max_write_ms_ = 0;
    upload_profile_close_ms_ = 0;
    upload_profile_chunks_ = 0;
    reset_upload_queue();
    upload_worker_enabled_ = false;

    Core1Job job{};
    job.type = Core1JobType::StorageOpenUpload;
    job.open_upload.transfer_id = transfer_id;
    job.open_upload.expected_size = size;
    job.open_upload.overwrite = overwrite != 0;
    std::strncpy(job.open_upload.filename, name, sizeof(job.open_upload.filename) - 1u);
    if (!worker_.submit_control(job)) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::Upload);
        return;
    }

    state_changed_ = true;
}

void DesktopProtocol::handle_file_upload_end(const char* params) {
    const auto& ctx = transfer_.context();
    if (transfer_.state() == StorageTransferState::UploadFinalizing) {
        process_upload_results();
        return;
    }
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
        const Core1WorkerSnapshot snapshot = worker_.snapshot();
        const bool pending = snapshot.bulk_queue_count > 0 ||
                             snapshot.result_queue_count > 0 ||
                             snapshot.busy;
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

    Core1Job job{};
    job.type = Core1JobType::StorageFinalizeUpload;
    job.finalize_upload.transfer_id = ctx.session_id;
    job.finalize_upload.delete_on_error = true;
    if (!worker_.submit_control(job)) {
        abort_active_storage_transfer(false, true);
        state_changed_ = true;
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::Upload);
        return;
    }

    upload_worker_enabled_ = false;
    transfer_.transition(StorageTransferState::UploadFinalizing);
    state_changed_ = true;
}

void DesktopProtocol::handle_file_upload_abort() {
    if (!admit_operation(OperationRequestType::UploadAbort, StorageTransferOperation::Upload, true)) {
        return;
    }

    abort_active_storage_transfer(true, true);
    state_changed_ = true;
    emit_ok("FILE_UPLOAD_ABORT");
}

// -- Download handlers --------------------------------------------------------

void DesktopProtocol::handle_file_download(const char* params) {
    if (transfer_active()) {
        char name[64] = {};
        if (transfer_.is_download() &&
            transfer_.state() == StorageTransferState::Downloading &&
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
    if (!admit_operation(OperationRequestType::DownloadBegin, StorageTransferOperation::Download, true)) {
        return;
    }

    const uint8_t transfer_id = next_transfer_id_;
    if (!transfer_.begin_download(msm_.state(), name, 0, transfer_id)) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::Download);
        return;
    }
    storage_request_seq_ = current_request_seq_;
    next_transfer_id_++;
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::Download);
        return;
    }

    reset_download_completion();
    download_read_jobs_pending_ = 0;

    Core1Job job{};
    job.type = Core1JobType::StorageOpenDownload;
    job.open_download.transfer_id = transfer_id;
    std::strncpy(job.open_download.filename, name, sizeof(job.open_download.filename) - 1u);
    if (!worker_.submit_control(job)) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::Download);
        return;
    }

    transfer_.set_expected_sequence(0);
    transfer_.set_crc_running(0xFFFFFFFFu);
    transfer_.set_retry_count(0);
    transfer_.set_awaiting_ack(false);
    transfer_.set_last_chunk_length(0);
    transfer_.set_last_send_ms(0);

    state_changed_ = true;
}

void DesktopProtocol::handle_file_download_abort() {
    if (!admit_operation(OperationRequestType::DownloadAbort, StorageTransferOperation::Download, true)) {
        return;
    }

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
    Core1Job job{};
    job.type = Core1JobType::StorageReadDownloadChunk;
    job.read_download.transfer_id = ctx.session_id;
    job.read_download.sequence = ctx.expected_sequence + download_read_jobs_pending_;
    if (!worker_.submit_bulk(job)) {
        return;
    }
    ++download_read_jobs_pending_;
}

void DesktopProtocol::process_download_opened(const Core1Result& result) {
    const auto& opened = result.open_download;
    if (!transfer_.is_download() || opened.transfer_id != transfer_.session_id()) {
        return;
    }

    if (opened.stat_result != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::FileNotFound, StorageTransferOperation::Download);
        return;
    }

    if (opened.open_result != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::Download);
        return;
    }

    transfer_.set_expected_size(opened.size);
    transfer_.transition(StorageTransferState::Downloading);
    send_download_ready();
    fill_download_window();
}

void DesktopProtocol::process_download_chunk_read(const Core1Result& result) {
    const auto& read = result.read_download;
    if (download_read_jobs_pending_ > 0) {
        --download_read_jobs_pending_;
    }

    if (!transfer_.is_download() || read.transfer_id != transfer_.session_id()) {
        return;
    }

    if (read.result != FR_OK) {
        abort_active_storage_transfer(false, false);
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::Download);
        return;
    }

    const auto& ctx = transfer_.context();
    if (read.length == 0) {
        const uint32_t final_crc = ~ctx.crc_running;
        const uint32_t final_seq = ctx.expected_sequence == 0 ? 0u : (ctx.expected_sequence - 1u);
        char name_copy[64]{};
        std::strncpy(name_copy, ctx.filename, sizeof(name_copy) - 1);
        const uint8_t session_id = ctx.session_id;
        const uint32_t size = ctx.bytes_sent;
        submit_download_close_job(session_id);
        download_read_jobs_pending_ = 0;
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

    if (read.sequence != ctx.expected_sequence) {
        char kv[64];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu EXPECTED=%lu",
                      static_cast<unsigned long>(read.sequence),
                      static_cast<unsigned long>(ctx.expected_sequence));
        abort_active_storage_transfer(false, false);
        emit_storage_error(StorageTransferError::BadSequence, StorageTransferOperation::Download, kv);
        return;
    }

    const uint32_t next_crc = crc32_update(ctx.crc_running, read.payload, read.length);
    const uint32_t bytes_sent = ctx.bytes_sent + read.length;
    const uint32_t send_time = to_ms_since_boot(get_absolute_time());

    transport_.send_frame(kDownloadDataFrameType,
                          ctx.session_id,
                          read.sequence,
                          read.payload,
                          read.length);
    transfer_.note_download_chunk_sent(read.sequence,
                                       read.length,
                                       bytes_sent,
                                       next_crc,
                                       send_time);
    fill_download_window();
}

void DesktopProtocol::submit_download_close_job(uint8_t transfer_id) {
    Core1Job job{};
    job.type = Core1JobType::StorageCloseDownload;
    job.close_download.transfer_id = transfer_id;
    worker_.submit_control(job);
}

bool DesktopProtocol::begin_file_list(uint32_t request_seq) {
    if (!transfer_.begin_listing(msm_.state())) {
        emit_storage_error(transfer_.last_error(), StorageTransferOperation::List);
        return true;
    }
    if (!msm_.sd_mounted()) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::List);
        return true;
    }

    storage_request_seq_ = request_seq;
    Core1Job job{};
    job.type = Core1JobType::StorageListBegin;
    job.intent = Core1JobIntent::ForegroundPreemptible;
    job.source = Core1JobSource::Desktop;
    if (!worker_.submit_control(job)) {
        transfer_.finish_operation();
        return false;
    }

    pending_file_list_request_ = false;
    return true;
}

void DesktopProtocol::service_pending_file_list_request() {
    if (!pending_file_list_request_ || transfer_.is_active()) {
        return;
    }

    const RequestDecision decision = coordinator_.decide(
        OperationRequest{OperationRequestType::FileList, OperationRequestSource::Desktop},
        msm_,
        transfer_,
        JobStreamState::Idle,
        worker_.snapshot(),
        storage_.state());

    if (decision.type == RequestDecisionType::AcceptNow ||
        decision.type == RequestDecisionType::PreemptAndAccept ||
        decision.type == RequestDecisionType::AbortCurrentAndAccept ||
        decision.type == RequestDecisionType::SuppressBackgroundAndAccept) {
        if (decision.type == RequestDecisionType::SuppressBackgroundAndAccept &&
            !suppress_background_worker_jobs(worker_)) {
            return;
        }
        begin_file_list(pending_file_list_request_seq_);
        return;
    }

    if (decision.type == RequestDecisionType::RejectBusy) {
        return;
    }

    storage_request_seq_ = pending_file_list_request_seq_;
    pending_file_list_request_ = false;
    if (storage_.state() != StorageState::Mounted || !msm_.sd_mounted()) {
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::List);
        return;
    }
    emit_storage_error(StorageTransferError::NotAllowed, StorageTransferOperation::List);
}

void DesktopProtocol::process_file_list_page(const Core1Result& result) {
    const auto& page = result.file_list;
    if (transfer_.operation() != StorageTransferOperation::List) {
        return;
    }

    if (page.result != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::ReadFail, StorageTransferOperation::List);
        return;
    }

    for (uint8_t index = 0; index < page.entry_count; ++index) {
        RespFileEntry payload{};
        payload.message_type = RESP_FILE_ENTRY;
        payload.request_seq = storage_request_seq_;
        write_wire_text(page.entries[index].name, payload.name, sizeof(payload.name));
        payload.size = page.entries[index].size;
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, storage_request_seq_, data, len);
        }, payload);
    }

    if (!page.complete) {
    Core1Job next{};
    next.type = Core1JobType::StorageListNextPage;
    next.intent = Core1JobIntent::ForegroundPreemptible;
    next.source = Core1JobSource::Desktop;
    if (!worker_.submit_control(next)) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::Busy, StorageTransferOperation::List);
        }
        return;
    }

    storage_.set_cached_free_bytes(page.free_bytes);
    RespFileListEnd payload{RESP_FILE_LIST_END,
                            storage_request_seq_,
                            page.total_count,
                            page.free_bytes};
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, storage_request_seq_, data, len);
    }, payload);
    transfer_.finish_operation();
}

void DesktopProtocol::process_file_deleted(const Core1Result& result) {
    const auto& deleted = result.delete_file;
    if (transfer_.operation() != StorageTransferOperation::Delete) {
        return;
    }

    if (deleted.result != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::FileNotFound, StorageTransferOperation::Delete);
        return;
    }

    const FileEntry* loaded = jobs_.loaded_entry();
    const bool deleted_loaded_job =
        loaded != nullptr && std::strcmp(loaded->name, deleted.filename) == 0;
    if (deleted_loaded_job) {
        try_unload_job();
        loaded_job_storage_.clear();
    }

    file_list_changed_ = true;
    Core1Job free_job{};
    free_job.type = Core1JobType::StorageFreeSpace;
    free_job.intent = Core1JobIntent::BackgroundDisposable;
    free_job.source = Core1JobSource::Desktop;
    worker_.submit_control(free_job);
    RespFileDelete payload{};
    payload.message_type = RESP_FILE_DELETE;
    payload.request_seq = storage_request_seq_;
    write_wire_text(deleted.filename, payload.name, sizeof(payload.name));
    send_binary_payload([&](const uint8_t* data, uint16_t len) {
        transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, storage_request_seq_, data, len);
    }, payload);
    if (deleted_loaded_job) {
        emit_state_update();
        emit_job();
    }
    transfer_.finish_operation();
}

void DesktopProtocol::process_free_space_ready(const Core1Result& result) {
    if (result.free_space.result == FR_OK) {
        storage_.set_cached_free_bytes(result.free_space.free_bytes);
    }
}

void DesktopProtocol::process_storage_health_ready(const Core1Result& result) {
    if (storage_.apply_worker_health_result(result.health.healthy, jobs_)) {
        on_sd_removed();
    }
}

// -- Upload helpers -----------------------------------------------------------

void DesktopProtocol::upload_abort_cleanup() {
    if (!transfer_.is_upload()) return;
    abort_active_storage_transfer(false, true);
}

void DesktopProtocol::submit_upload_abort_job(uint8_t transfer_id,
                                              const char* filename,
                                              bool delete_partial) {
    Core1Job job{};
    job.type = Core1JobType::StorageAbortTransfer;
    job.abort_transfer.transfer_id = transfer_id;
    job.abort_transfer.delete_partial = delete_partial;
    if (filename != nullptr) {
        std::strncpy(job.abort_transfer.filename,
                     filename,
                     sizeof(job.abort_transfer.filename) - 1u);
    }

    if (!worker_.submit_urgent(job)) {
        return;
    }

    const uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        const Core1WorkerSnapshot snapshot = worker_.snapshot();
        if (!snapshot.busy && snapshot.urgent_queue_count == 0) {
            break;
        }
        if ((to_ms_since_boot(get_absolute_time()) - start_ms) > 1000u) {
            break;
        }
        sleep_us(100);
    }
    worker_.clear_results();
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
    const uint32_t next_crc = crc32_update(upload_receive_crc_running_, payload, payload_len);

    Core1Job job{};
    job.type = Core1JobType::StorageWriteUploadChunk;
    job.upload.transfer_id = transfer_id;
    job.upload.sequence = sequence;
    job.upload.length = payload_len;
    job.upload.crc_after = next_crc;
    std::memcpy(job.upload.payload, payload, payload_len);

    const uint32_t enqueue_start_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        process_upload_results();
        if (!transfer_.is_upload()) {
            return false;
        }

        const bool queued = upload_worker_enabled_ && worker_.submit_bulk(job);

        if (queued) {
            const Core1WorkerSnapshot snapshot = worker_.snapshot();
            if (snapshot.bulk_queue_high_water > upload_queue_high_water_) {
                upload_queue_high_water_ = snapshot.bulk_queue_high_water;
            }
            ++upload_next_receive_sequence_;
            upload_bytes_received_ += payload_len;
            upload_receive_crc_running_ = next_crc;
            return true;
        }

        if ((to_ms_since_boot(get_absolute_time()) - enqueue_start_ms) > 10000u) {
            upload_abort_cleanup();
            char kv[64];
            std::snprintf(kv,
                          sizeof(kv),
                          "SEQ=%lu REASON=QUEUE_FULL",
                          static_cast<unsigned long>(sequence));
            emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
            state_changed_ = true;
            return false;
        }

        sleep_us(100);
    }
}

void DesktopProtocol::process_upload_results() {
    while (true) {
        Core1Result result{};
        if (!worker_.try_pop_result(result)) {
            return;
        }
        switch (result.type) {
            case Core1ResultType::FileOpened:
                handle_upload_opened(result);
                break;
            case Core1ResultType::UploadChunkCommitted:
                commit_upload_result(result);
                break;
            case Core1ResultType::FileClosed:
                handle_upload_finalized(result);
                break;
            case Core1ResultType::DownloadOpened:
                process_download_opened(result);
                break;
            case Core1ResultType::DownloadChunkRead:
                process_download_chunk_read(result);
                break;
            case Core1ResultType::DownloadClosed:
                break;
            case Core1ResultType::FileListPage:
                process_file_list_page(result);
                break;
            case Core1ResultType::FileDeleted:
                process_file_deleted(result);
                break;
            case Core1ResultType::FreeSpaceReady:
                process_free_space_ready(result);
                break;
            case Core1ResultType::StorageHealthReady:
                process_storage_health_ready(result);
                break;
            case Core1ResultType::TransferAborted:
            case Core1ResultType::None:
            case Core1ResultType::StorageError:
            case Core1ResultType::WorkerFault:
            default:
                break;
        }
    }
}

void DesktopProtocol::handle_upload_opened(const Core1Result& result) {
    const auto& open = result.open_upload;
    if (!transfer_.is_upload() || open.transfer_id != transfer_.session_id()) {
        return;
    }

    if (open.open_result == FR_EXIST) {
        transfer_.finish_operation();
        RespError payload{};
        payload.message_type = RESP_ERROR;
        payload.request_seq = response_request_seq();
        payload.error = ERROR_UPLOAD_FILE_EXISTS;
        write_wire_text(open.filename, payload.reason, sizeof(payload.reason));
        send_binary_payload([&](const uint8_t* data, uint16_t len) {
            transport_.send_frame(kResponseFrameType, PCNC_TRANSFER_ID_NONE, response_request_seq(), data, len);
        }, payload);
        return;
    }

    if (open.open_result == FR_DENIED) {
        transfer_.finish_operation();
        char kv[64];
        std::snprintf(kv, sizeof(kv), "FREE=%llu",
                      static_cast<unsigned long long>(open.free_bytes));
        emit_storage_error(StorageTransferError::NoSpace, StorageTransferOperation::Upload, kv);
        return;
    }

    if (open.open_result != FR_OK) {
        transfer_.finish_operation();
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload);
        return;
    }

    upload_profile_prealloc_ms_ = open.prealloc_elapsed_ms;
    if (open.prealloc_result != FR_OK) {
        char kv[96];
        std::snprintf(kv,
                      sizeof(kv),
                      "PHASE=PREALLOC FRESULT=%d SIZE=%lu",
                      static_cast<int>(open.prealloc_result),
                      static_cast<unsigned long>(open.expected_size));
        transfer_.finish_operation();
        emit_storage_error(open.prealloc_result == FR_DENIED
                               ? StorageTransferError::NoSpace
                               : StorageTransferError::WriteFail,
                           StorageTransferOperation::Upload,
                           kv);
        return;
    }

    transfer_.set_expected_sequence(0);
    transfer_.set_last_ack_sequence(0xFFFFFFFFu);
    transfer_.set_retry_count(0);
    upload_worker_enabled_ = true;

    state_changed_ = true;
    send_upload_ready();
}

void DesktopProtocol::handle_upload_finalized(const Core1Result& result) {
    const auto& finalized = result.finalize_upload;
    if (!transfer_.is_upload() || finalized.transfer_id != transfer_.session_id()) {
        return;
    }

    const auto& ctx = transfer_.context();
    char name_copy[64]{};
    std::strncpy(name_copy, ctx.filename, sizeof(name_copy) - 1u);
    const uint32_t bytes_copy = ctx.bytes_written;
    const uint32_t final_crc = ~ctx.crc_running;
    const uint8_t session_id = ctx.session_id;

    upload_profile_close_ms_ = finalized.close_elapsed_ms;
    if (finalized.result != FR_OK) {
        char kv[96];
        std::snprintf(kv,
                      sizeof(kv),
                      "PHASE=CLOSE FRESULT=%d WRITTEN=%lu SIZE=%lu",
                      static_cast<int>(finalized.result),
                      static_cast<unsigned long>(bytes_copy),
                      static_cast<unsigned long>(ctx.expected_size));
        transfer_.finish_operation();
        reset_upload_queue();
        state_changed_ = true;
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
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
    Core1Job free_job{};
    free_job.type = Core1JobType::StorageFreeSpace;
    free_job.intent = Core1JobIntent::BackgroundDisposable;
    free_job.source = Core1JobSource::Desktop;
    worker_.submit_control(free_job);
    state_changed_ = true;
    send_upload_complete();
}

void DesktopProtocol::commit_upload_result(const Core1Result& result) {
    const auto& ctx = transfer_.context();
    const auto& upload = result.upload;
    if (!transfer_.is_upload() || upload.transfer_id != ctx.session_id) {
        return;
    }
    if (upload.sequence != ctx.expected_sequence) {
        char kv[64];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu EXPECTED=%lu",
                      static_cast<unsigned long>(upload.sequence),
                      static_cast<unsigned long>(ctx.expected_sequence));
        emit_storage_error(StorageTransferError::BadSequence, StorageTransferOperation::Upload, kv);
        return;
    }

    upload_profile_write_ms_ += upload.write_elapsed_ms;
    if (upload.write_elapsed_ms > upload_profile_max_write_ms_) {
        upload_profile_max_write_ms_ = upload.write_elapsed_ms;
    }
    ++upload_profile_chunks_;
    if (upload.result != FR_OK || upload.written != upload.length) {
        upload_abort_cleanup();
        char kv[32];
        std::snprintf(kv, sizeof(kv), "SEQ=%lu", static_cast<unsigned long>(upload.sequence));
        emit_storage_error(StorageTransferError::WriteFail, StorageTransferOperation::Upload, kv);
        state_changed_ = true;
        return;
    }

    const uint32_t bytes_written = ctx.bytes_written + upload.length;
    transfer_.note_upload_chunk_committed(upload.sequence, bytes_written, upload.crc_after);
    if ((upload.sequence % 64u) == 0u || bytes_written >= ctx.expected_size) {
        const uint32_t total_ms = to_ms_since_boot(get_absolute_time()) - upload_profile_start_ms_;
        const uint32_t bps = total_ms > 0
            ? static_cast<uint32_t>((static_cast<uint64_t>(bytes_written) * 1000u) / total_ms)
            : 0u;
        const Core1WorkerSnapshot snapshot = worker_.snapshot();
        const size_t queue_count = snapshot.bulk_queue_count;
        char profile_kv[192];
        std::snprintf(profile_kv,
                      sizeof(profile_kv),
                      "SEQ=%lu BYTES=%lu TOTAL_MS=%lu WRITE_MS=%lu LAST_WRITE_MS=%lu MAX_WRITE_MS=%lu CHUNKS=%lu QUEUE=%lu QUEUE_MAX=%lu BPS=%lu",
                      static_cast<unsigned long>(upload.sequence),
                      static_cast<unsigned long>(bytes_written),
                      static_cast<unsigned long>(total_ms),
                      static_cast<unsigned long>(upload_profile_write_ms_),
                      static_cast<unsigned long>(upload.write_elapsed_ms),
                      static_cast<unsigned long>(upload_profile_max_write_ms_),
                      static_cast<unsigned long>(upload_profile_chunks_),
                      static_cast<unsigned long>(queue_count),
                      static_cast<unsigned long>(upload_queue_high_water_),
                      static_cast<unsigned long>(bps));
        emit_event_kv("STORAGE_UPLOAD_CHUNK_PROFILE", profile_kv);
    }

    const bool should_ack =
        ((upload.sequence + 1u) % kUploadAckStride) == 0u ||
        bytes_written >= ctx.expected_size;
    if (should_ack) {
        send_upload_chunk_ack(upload.sequence, bytes_written);
    }
}

void DesktopProtocol::reset_upload_queue() {
    upload_worker_enabled_ = false;
    worker_.clear_pending_jobs();
    while (worker_.snapshot().busy) {
        sleep_us(50);
    }
    worker_.clear_results();
    upload_queue_high_water_ = 0;
    upload_next_receive_sequence_ = 0;
    upload_bytes_received_ = 0;
    upload_receive_crc_running_ = 0xFFFFFFFFu;
}

// -- Storage callbacks --------------------------------------------------------

void DesktopProtocol::on_sd_mounted() {
    inject(MachineEvent::SdMounted);
    emit_state_update();
    emit_event("SD_MOUNTED");
}

void DesktopProtocol::on_sd_removed() {
    if (pending_file_list_request_) {
        storage_request_seq_ = pending_file_list_request_seq_;
        pending_file_list_request_ = false;
        emit_storage_error(StorageTransferError::SdNotMounted, StorageTransferOperation::List);
    }
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
