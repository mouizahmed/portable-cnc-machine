#include "app/comm/pico_uart_client.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

namespace {
bool starts_with(const char* text, const char* prefix) {
    return text != nullptr &&
           prefix != nullptr &&
           std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}
}  // namespace

PicoUartClient::PicoUartClient(MachineFsm& machine_fsm,
                               JogStateMachine& jog_state_machine,
                               JobStateMachine& job_state_machine,
                               const StorageService& storage_service,
                               Core1Worker& worker,
                               JobStreamStateMachine& job_stream)
    : machine_fsm_(machine_fsm),
      jog_state_machine_(jog_state_machine),
      job_state_machine_(job_state_machine),
      storage_service_(storage_service),
      worker_(worker),
      job_stream_(job_stream) {
    init_transport();
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    last_rx_ms_ = now;
    last_probe_ms_ = 0;
    motion_snapshot_.linked = false;
}

PicoUartPollResult PicoUartClient::poll() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    PicoUartPollResult result = process_input(now);

    if (pending_job_progress_) {
        pending_job_progress_ = false;
        result.job_progress = true;
        result.progress_line = pending_progress_line_;
        result.progress_total = pending_progress_total_;
    }
    if (pending_job_complete_) {
        pending_job_complete_ = false;
        result.job_complete = true;
    }
    if (pending_job_error_) {
        pending_job_error_ = false;
        result.job_error = true;
        std::strncpy(result.error_reason, pending_error_reason_, sizeof(result.error_reason) - 1u);
        result.error_reason[sizeof(result.error_reason) - 1u] = '\0';
    }

    if (job_stream_.is_active() &&
        !stream_waiting_for_ok_ &&
        !stream_batch_pending_ &&
        now >= stream_next_eligible_ms_ &&
        (machine_fsm_.state() == MachineOperationState::Idle ||
         machine_fsm_.state() == MachineOperationState::Starting)) {
        if (!send_next_stream_line(now)) {
            mark_stream_fault("STREAM_SEND_FAILED");
            result.machine_changed = true;
        }
        if (pending_job_complete_) {
            result.machine_changed = machine_fsm_.handle_event(MachineEvent::GrblIdle) || result.machine_changed;
        }
    }

    if (motion_snapshot_.command_in_flight &&
        (now - command_started_ms_) >= kForegroundCommandTimeoutMs) {
        clear_command_in_flight();
    }

    service_background_telemetry(now);

    const uint32_t quiet_for = now - last_rx_ms_;
    if (quiet_for < kSilentLinkThresholdMs) {
        return result;
    }

    if (linked_ && missed_probes_ >= kMaxMissedProbes) {
        merge(result, mark_link_disconnected());
    }

    return result;
}

bool PicoUartClient::select_file(int16_t index) {
    return index >= 0 && index < static_cast<int16_t>(job_state_machine_.count());
}

bool PicoUartClient::upload_and_run_loaded_job() {
    return start_streaming_loaded_job();
}

bool PicoUartClient::start_streaming_loaded_job() {
    if (job_stream_.is_active() || !linked_) {
        return false;
    }

    const FileEntry* entry = job_state_machine_.loaded_entry();
    const int16_t index = job_state_machine_.loaded_index();
    if (entry == nullptr || index < 0 || !storage_service_.is_mounted()) {
        return false;
    }

    Core1Job job{};
    job.type = Core1JobType::JobStreamPrepareBegin;
    job.intent = Core1JobIntent::ForegroundExclusive;
    job.source = Core1JobSource::System;
    job.stream_prepare.loaded_index = index;
    std::strncpy(job.stream_prepare.filename, entry->name, sizeof(job.stream_prepare.filename) - 1u);

    if (!worker_.submit_control(job)) {
        return false;
    }

    machine_fsm_.handle_event(MachineEvent::StartCmd);
    job_stream_.start_prepare(index, entry->name);
    stream_batch_pending_ = false;
    stream_cancel_pending_ = false;
    stream_waiting_for_ok_ = false;
    stream_batch_complete_ = false;
    stream_batch_line_count_ = 0;
    stream_batch_next_line_ = 0;
    stream_next_eligible_ms_ = 0;
    pending_job_progress_ = false;
    pending_job_complete_ = false;
    pending_job_error_ = false;
    last_progress_emit_ms_ = to_ms_since_boot(get_absolute_time());
    note_command_started(MotionCommandType::JobStart, false, to_ms_since_boot(get_absolute_time()));
    return true;
}

bool PicoUartClient::hold() {
    const bool sent = send_raw_line("@RT_FEED_HOLD");
    if (sent) {
        note_command_started(MotionCommandType::Hold, true, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

bool PicoUartClient::resume() {
    const bool sent = send_raw_line("@RT_CYCLE_START");
    if (sent) {
        note_command_started(MotionCommandType::Resume, false, to_ms_since_boot(get_absolute_time()));
        job_stream_.note_resume();
    }
    return sent;
}

bool PicoUartClient::abort() {
    if (job_stream_.is_active()) {
        cancel_stream(true);
        return true;
    }

    const bool sent = send_raw_line("@RT_RESET");
    if (sent) {
        note_command_started(MotionCommandType::Abort, true, to_ms_since_boot(get_absolute_time()));
        machine_fsm_.handle_event(MachineEvent::AbortCmd);
    }
    return sent;
}

bool PicoUartClient::estop() {
    const bool sent = send_raw_line("@RT_ESTOP");
    if (sent) {
        note_command_started(MotionCommandType::Estop, true, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

bool PicoUartClient::reset() {
    const bool sent = send_raw_line("@UNLOCK");
    if (sent) {
        note_command_started(MotionCommandType::Reset, true, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

bool PicoUartClient::jog_cancel() {
    const bool sent = send_raw_line("@JOG_CANCEL");
    if (sent) {
        note_command_started(MotionCommandType::JogCancel, true, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

bool PicoUartClient::jog(JogAction action) {
    char axis = '\0';
    float distance = 0.0f;

    switch (action) {
        case JogAction::MoveXNegative:
            axis = 'X';
            distance = -jog_state_machine_.step_size_mm();
            break;
        case JogAction::MoveXPositive:
            axis = 'X';
            distance = jog_state_machine_.step_size_mm();
            break;
        case JogAction::MoveYNegative:
            axis = 'Y';
            distance = -jog_state_machine_.step_size_mm();
            break;
        case JogAction::MoveYPositive:
            axis = 'Y';
            distance = jog_state_machine_.step_size_mm();
            break;
        case JogAction::MoveZNegative:
            axis = 'Z';
            distance = -jog_state_machine_.step_size_mm();
            break;
        case JogAction::MoveZPositive:
            axis = 'Z';
            distance = jog_state_machine_.step_size_mm();
            break;
        default:
            return false;
    }

    const bool sent = jog_axis(axis, distance, jog_state_machine_.feed_rate_mm_min());
    if (sent) {
        note_command_started(MotionCommandType::Jog, false, to_ms_since_boot(get_absolute_time()));
        machine_fsm_.handle_event(MachineEvent::JogCmd);
    }
    return sent;
}

bool PicoUartClient::jog_axis(char axis, float distance_mm, uint16_t feed_mm_min) {
    return send_command("@JOG AXIS=%c DIST=%.3f FEED=%u",
                        axis,
                        static_cast<double>(distance_mm),
                        static_cast<unsigned int>(feed_mm_min));
}

bool PicoUartClient::home_all() {
    const bool sent = send_raw_line("@HOME");
    if (sent) {
        note_command_started(MotionCommandType::HomeAll, false, to_ms_since_boot(get_absolute_time()));
        machine_fsm_.handle_event(MachineEvent::HomeCmd);
    }
    return sent;
}

bool PicoUartClient::zero_all() {
    const bool sent = zero_axis("ALL");
    if (sent) {
        note_command_started(MotionCommandType::ZeroAll, false, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

bool PicoUartClient::zero_axis(const char* axis) {
    if (axis == nullptr || *axis == '\0') {
        axis = "ALL";
    }
    return send_command("@ZERO AXIS=%s", axis);
}

bool PicoUartClient::spindle_on(uint16_t rpm) {
    const bool sent = send_command("@SPINDLE_ON RPM=%u", static_cast<unsigned int>(rpm));
    if (sent) {
        note_command_started(MotionCommandType::SpindleOn, false, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

bool PicoUartClient::spindle_off() {
    const bool sent = send_raw_line("@SPINDLE_OFF");
    if (sent) {
        note_command_started(MotionCommandType::SpindleOff, false, to_ms_since_boot(get_absolute_time()));
    }
    return sent;
}

const char* PicoUartClient::link_status() const {
    return linked_ ? "UART" : "DISC";
}

JobStreamState PicoUartClient::job_stream_state() const {
    return job_stream_.state();
}

MotionLinkSnapshot PicoUartClient::motion_snapshot() const {
    return motion_snapshot_;
}

void PicoUartClient::handle_worker_result(const Core1Result& result) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    switch (result.type) {
        case Core1ResultType::StreamPrepareReady:
            if (result.stream_prepare.result != FR_OK || result.stream_prepare.total_lines == 0) {
                mark_stream_fault("STREAM_PREPARE_FAILED");
                return;
            }
            job_stream_.note_prepare_ready(result.stream_prepare.total_lines);
            job_stream_.note_beginning();
            publish_stream_progress(true, now);
            request_next_stream_batch();
            return;

        case Core1ResultType::StreamLineBatchReady:
            stream_batch_pending_ = false;
            if (result.stream_batch.result != FR_OK) {
                mark_stream_fault("STREAM_READ_FAILED");
                return;
            }

            stream_batch_line_count_ = result.stream_batch.line_count;
            stream_batch_next_line_ = 0;
            stream_batch_complete_ = result.stream_batch.complete;
            for (uint8_t i = 0; i < result.stream_batch.line_count; ++i) {
                std::strncpy(stream_batch_lines_[i],
                             result.stream_batch.lines[i],
                             sizeof(stream_batch_lines_[i]) - 1u);
                stream_batch_lines_[i][sizeof(stream_batch_lines_[i]) - 1u] = '\0';
            }

            if (stream_batch_line_count_ == 0 && stream_batch_complete_) {
                finish_stream_if_ready();
                return;
            }

            if (!send_next_stream_line(now)) {
                mark_stream_fault("STREAM_SEND_FAILED");
            }
            return;

        case Core1ResultType::StreamCancelled:
            stream_batch_pending_ = false;
            stream_cancel_pending_ = false;
            stream_waiting_for_ok_ = false;
            stream_batch_line_count_ = 0;
            stream_batch_next_line_ = 0;
            if (job_stream_.state() == JobStreamState::Cancelling &&
                machine_fsm_.state() == MachineOperationState::Starting) {
                machine_fsm_.handle_event(MachineEvent::GrblIdle);
            }
            job_stream_.reset();
            clear_command_in_flight();
            return;

        case Core1ResultType::WorkerFault:
            if (result.source_job == Core1JobType::JobStreamPrepareBegin ||
                result.source_job == Core1JobType::JobStreamPrepareNextBatch ||
                result.source_job == Core1JobType::JobStreamCancel) {
                mark_stream_fault("STREAM_WORKER_FAULT");
            }
            return;

        default:
            return;
    }
}

void PicoUartClient::init_transport() {
    if (transport_initialized_) {
        return;
    }

    uart_init(uart0, MOTION_UART_BAUD);
    gpio_set_function(PIN_MOTION_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_MOTION_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);
    transport_initialized_ = true;
}

bool PicoUartClient::send_probe() {
    return send_ping();
}

bool PicoUartClient::send_ping() {
    return send_raw_line("@PING");
}

bool PicoUartClient::send_command(const char* fmt, ...) {
    char buffer[200];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return send_raw_line(buffer);
}

bool PicoUartClient::send_raw_line(const char* line) {
    if (line == nullptr || *line == '\0') {
        return false;
    }

    const size_t line_len = std::strlen(line);
    uart_write_blocking(uart0, reinterpret_cast<const uint8_t*>(line), line_len);
    uart_write_blocking(uart0, reinterpret_cast<const uint8_t*>("\n"), 1);
    return true;
}

uint32_t PicoUartClient::next_seq() {
    return next_seq_++;
}

PicoUartPollResult PicoUartClient::process_input(uint32_t now) {
    PicoUartPollResult result{};

    while (uart_is_readable(uart0)) {
        const int c = uart_getc(uart0);
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            rx_line_[rx_length_] = '\0';
            merge(result, handle_line(rx_line_, now));
            rx_length_ = 0;
            rx_line_[0] = '\0';
            continue;
        }

        if (rx_length_ + 1 >= sizeof(rx_line_)) {
            rx_length_ = 0;
            rx_line_[0] = '\0';
            continue;
        }

        rx_line_[rx_length_++] = static_cast<char>(c);
    }

    return result;
}

PicoUartPollResult PicoUartClient::handle_line(const char* line, uint32_t now) {
    PicoUartPollResult result{};
    if (line == nullptr || *line == '\0') {
        return result;
    }

    if (std::strcmp(line, "ok") == 0) {
        return handle_gcode_ok(now);
    }

    if (starts_with(line, "error:")) {
        return handle_gcode_error(line, now);
    }

    if (line[0] != '@') {
        return result;
    }

    if (starts_with(line, "@PONG")) {
        mark_valid_traffic(now);
        return mark_link_connected(now);
    }

    if (starts_with(line, "@ACK")) {
        return handle_ack_line(line, now);
    }

    if (starts_with(line, "@BOOT")) {
        return handle_boot_line(line, now);
    }

    if (starts_with(line, "@GRBL_STATE")) {
        return handle_grbl_state_line(line, now);
    }

    if (starts_with(line, "@POS")) {
        PicoUartPollResult pos_result = mark_link_connected(now);
        float mx = jog_state_machine_.x();
        float my = jog_state_machine_.y();
        float mz = jog_state_machine_.z();
        const bool ok_x = extract_float(line, "MX=", mx);
        const bool ok_y = extract_float(line, "MY=", my);
        const bool ok_z = extract_float(line, "MZ=", mz);
        if (ok_x && ok_y && ok_z) {
            jog_state_machine_.set_position(mx, my, mz);
            pos_result.position_changed = true;
        }
        return pos_result;
    }

    if (starts_with(line, "@ERR")) {
        return handle_error_line(line, now);
    }

    return result;
}

PicoUartPollResult PicoUartClient::handle_ack_line(const char* line, uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);

    if (std::strstr(line, "PONG=1") != nullptr) {
        motion_snapshot_.telemetry_pending = false;
        return result;
    }

    if (motion_snapshot_.active_command == MotionCommandType::ZeroAll) {
        clear_command_in_flight();
    }

    if (motion_snapshot_.active_command == MotionCommandType::Abort &&
        job_stream_.state() == JobStreamState::Cancelling &&
        machine_fsm_.state() == MachineOperationState::Starting) {
        result.machine_changed = machine_fsm_.handle_event(MachineEvent::GrblIdle) || result.machine_changed;
    }

    return result;
}

PicoUartPollResult PicoUartClient::handle_boot_line(const char* line, uint32_t now) {
    PicoUartPollResult result{};
    if (!starts_with(line, "@BOOT TEENSY_READY")) {
        return result;
    }

    mark_valid_traffic(now);
    return mark_link_connected(now);
}

PicoUartPollResult PicoUartClient::handle_grbl_state_line(const char* line, uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);

    char state_text[24] = {};
    if (!extract_token(line, "@GRBL_STATE ", state_text, sizeof(state_text))) {
        return result;
    }

    char substate[24] = {};
    extract_token(line, "SUBSTATE=", substate, sizeof(substate));

    merge(result, handle_machine_state(state_text, substate, now));
    return result;
}

PicoUartPollResult PicoUartClient::handle_error_line(const char* line, uint32_t now) {
    (void)line;
    PicoUartPollResult result = mark_link_connected(now);
    clear_command_in_flight();
    if (job_stream_.is_active()) {
        mark_stream_fault();
    }
    const bool fault_changed = machine_fsm_.handle_event(MachineEvent::GrblAlarm);
    result.machine_changed = result.machine_changed || fault_changed;
    return result;
}

void PicoUartClient::mark_valid_traffic(uint32_t now) {
    last_rx_ms_ = now;
    missed_probes_ = 0;
}

PicoUartPollResult PicoUartClient::mark_link_connected(uint32_t now) {
    PicoUartPollResult result{};
    mark_valid_traffic(now);
    motion_snapshot_.linked = true;

    if (!linked_) {
        linked_ = true;
        result.link_transition = MotionLinkTransition::Connected;
    }

    const bool changed = machine_fsm_.handle_event(MachineEvent::TeensyConnected);
    result.machine_changed = result.machine_changed || changed;
    return result;
}

PicoUartPollResult PicoUartClient::mark_link_disconnected() {
    PicoUartPollResult result{};
    if (!linked_) {
        return result;
    }

    linked_ = false;
    missed_probes_ = 0;
    motion_snapshot_.linked = false;
    clear_command_in_flight();
    if (job_stream_.is_active()) {
        cancel_stream(false);
    }
    result.link_transition = MotionLinkTransition::Disconnected;
    result.machine_changed = machine_fsm_.handle_event(MachineEvent::TeensyDisconnected);
    return result;
}

void PicoUartClient::merge(PicoUartPollResult& into, const PicoUartPollResult& from) {
    into.machine_changed = into.machine_changed || from.machine_changed;
    into.job_changed = into.job_changed || from.job_changed;
    into.position_changed = into.position_changed || from.position_changed;
    if (from.job_progress) {
        into.job_progress = true;
        into.progress_line = from.progress_line;
        into.progress_total = from.progress_total;
    }
    into.job_complete = into.job_complete || from.job_complete;
    if (from.job_error) {
        into.job_error = true;
        std::strncpy(into.error_reason, from.error_reason, sizeof(into.error_reason) - 1u);
        into.error_reason[sizeof(into.error_reason) - 1u] = '\0';
    }
    if (from.link_transition != MotionLinkTransition::None) {
        into.link_transition = from.link_transition;
    }
}

void PicoUartClient::note_command_started(MotionCommandType command, bool urgent, uint32_t now) {
    motion_snapshot_.active_command = command;
    motion_snapshot_.command_in_flight = command != MotionCommandType::TelemetryProbe;
    motion_snapshot_.urgent_pending = urgent;
    command_started_ms_ = now;
}

void PicoUartClient::clear_command_in_flight() {
    motion_snapshot_.active_command = MotionCommandType::None;
    motion_snapshot_.command_in_flight = false;
    motion_snapshot_.urgent_pending = false;
    command_started_ms_ = 0;
}

void PicoUartClient::mark_stream_fault() {
    mark_stream_fault("STREAM_ERROR");
}

void PicoUartClient::mark_stream_fault(const char* reason) {
    stream_batch_pending_ = false;
    stream_cancel_pending_ = false;
    stream_waiting_for_ok_ = false;
    stream_batch_line_count_ = 0;
    stream_batch_next_line_ = 0;
    stream_next_eligible_ms_ = 0;
    clear_command_in_flight();
    if (job_stream_.is_active()) {
        job_stream_.note_fault();
    }
    pending_job_error_ = true;
    std::strncpy(pending_error_reason_, reason != nullptr ? reason : "STREAM_ERROR", sizeof(pending_error_reason_) - 1u);
    pending_error_reason_[sizeof(pending_error_reason_) - 1u] = '\0';
    if (machine_fsm_.state() == MachineOperationState::Starting) {
        machine_fsm_.handle_event(MachineEvent::AbortCmd);
        machine_fsm_.handle_event(MachineEvent::GrblIdle);
    }
}

void PicoUartClient::cancel_stream(bool send_abort_command) {
    if (!job_stream_.is_active()) {
        return;
    }

    job_stream_.note_cancelling();
    if (!stream_cancel_pending_) {
        Core1Job cancel_job{};
        cancel_job.type = Core1JobType::JobStreamCancel;
        cancel_job.intent = Core1JobIntent::Urgent;
        cancel_job.source = Core1JobSource::System;
        cancel_job.stream_cancel.loaded_index = job_stream_.loaded_index();
        if (worker_.submit_urgent(cancel_job)) {
            stream_cancel_pending_ = true;
        }
    }

    if (send_abort_command && linked_) {
        send_raw_line("@RT_RESET");
        note_command_started(MotionCommandType::Abort, true, to_ms_since_boot(get_absolute_time()));
        machine_fsm_.handle_event(MachineEvent::AbortCmd);
    }
}

bool PicoUartClient::request_next_stream_batch() {
    if (stream_batch_pending_) {
        return true;
    }

    Core1Job job{};
    job.type = Core1JobType::JobStreamPrepareNextBatch;
    job.intent = Core1JobIntent::ForegroundExclusive;
    job.source = Core1JobSource::System;
    job.stream_batch.loaded_index = job_stream_.loaded_index();
    stream_batch_pending_ = worker_.submit_bulk(job);
    return stream_batch_pending_;
}

bool PicoUartClient::send_next_stream_line(uint32_t now) {
    if (stream_waiting_for_ok_) {
        return true;
    }

    if (stream_batch_next_line_ >= stream_batch_line_count_) {
        if (stream_batch_complete_) {
            finish_stream_if_ready();
            return true;
        }
        return request_next_stream_batch();
    }

    char wire_line[kCore1WorkerStreamLineBytes + 8]{};
    std::snprintf(wire_line,
                  sizeof(wire_line),
                  "@GCODE %s",
                  stream_batch_lines_[stream_batch_next_line_]);
    if (!send_raw_line(wire_line)) {
        return false;
    }

    stream_waiting_for_ok_ = true;
    note_command_started(MotionCommandType::JobStart, false, now);
    return true;
}

void PicoUartClient::publish_stream_progress(bool force, uint32_t now) {
    const uint32_t sent = job_stream_.lines_sent();
    const uint32_t total = job_stream_.total_lines();
    if (!force && (sent % 10u) != 0u && (now - last_progress_emit_ms_) < 500u) {
        return;
    }

    pending_job_progress_ = true;
    pending_progress_line_ = sent;
    pending_progress_total_ = total;
    last_progress_emit_ms_ = now;
}

void PicoUartClient::finish_stream_if_ready() {
    if (stream_waiting_for_ok_) {
        return;
    }

    job_stream_.note_complete();
    machine_fsm_.notify_stream_complete();
    pending_job_complete_ = true;
    clear_command_in_flight();
}

PicoUartPollResult PicoUartClient::handle_gcode_ok(uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);
    clear_command_in_flight();

    if (!job_stream_.is_active() && job_stream_.state() != JobStreamState::Complete) {
        return result;
    }

    if (stream_waiting_for_ok_) {
        stream_waiting_for_ok_ = false;
        ++stream_batch_next_line_;
        job_stream_.note_lines_sent(1);
        publish_stream_progress(false, now);
        stream_next_eligible_ms_ = now + 50u;
    }

    if (job_stream_.state() == JobStreamState::PausedByHold) {
        return result;
    }

    return result;
}

PicoUartPollResult PicoUartClient::handle_gcode_error(const char* line, uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);
    send_raw_line("@RT_RESET");
    mark_stream_fault(line != nullptr ? line : "GCODE_ERROR");
    result.machine_changed = true;
    return result;
}

void PicoUartClient::service_background_telemetry(uint32_t now) {
    if ((now - last_rx_ms_) < kSilentLinkThresholdMs) {
        motion_snapshot_.telemetry_pending = false;
        return;
    }

    if ((now - last_probe_ms_) < kProbeIntervalMs) {
        return;
    }

    motion_snapshot_.telemetry_pending = true;
    if (motion_snapshot_.command_in_flight || motion_snapshot_.urgent_pending || job_stream_.is_active()) {
        return;
    }

    if (send_probe()) {
        last_probe_ms_ = now;
        if (missed_probes_ < kMaxMissedProbes) {
            ++missed_probes_;
        }
        motion_snapshot_.telemetry_pending = false;
    }
}

bool PicoUartClient::extract_token(const char* line, const char* key, char* out, std::size_t size) {
    if (line == nullptr || key == nullptr || out == nullptr || size == 0) {
        return false;
    }

    const char* start = std::strstr(line, key);
    if (start == nullptr) {
        return false;
    }
    start += std::strlen(key);

    std::size_t index = 0;
    while (start[index] != '\0' && start[index] != ' ' && index + 1 < size) {
        out[index] = start[index];
        ++index;
    }
    out[index] = '\0';
    return index > 0;
}

bool PicoUartClient::extract_int(const char* line, const char* key, int& out) {
    char buffer[24];
    if (!extract_token(line, key, buffer, sizeof(buffer))) {
        return false;
    }
    out = std::atoi(buffer);
    return true;
}

bool PicoUartClient::extract_float(const char* line, const char* key, float& out) {
    char buffer[24];
    if (!extract_token(line, key, buffer, sizeof(buffer))) {
        return false;
    }
    out = std::strtof(buffer, nullptr);
    return true;
}

PicoUartPollResult PicoUartClient::handle_machine_state(const char* text,
                                                        const char* substate,
                                                        uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);
    if (text == nullptr) {
        return result;
    }

    bool changed = false;
    if (std::strcmp(text, "IDLE") == 0) {
        clear_command_in_flight();
        changed = machine_fsm_.handle_event(MachineEvent::GrblIdle);
        if (job_stream_.state() == JobStreamState::Cancelling) {
            job_stream_.reset();
        } else if (job_stream_.state() == JobStreamState::Complete) {
            job_stream_.note_complete();
        } else if (job_stream_.is_active() && !stream_waiting_for_ok_ && !stream_batch_pending_) {
            stream_next_eligible_ms_ = now;
        }
    } else if (std::strcmp(text, "HOMING") == 0) {
        clear_command_in_flight();
        changed = machine_fsm_.handle_event(MachineEvent::GrblHoming);
    } else if (std::strcmp(text, "CYCLE") == 0 || std::strcmp(text, "RUNNING") == 0) {
        clear_command_in_flight();
        if (job_stream_.state() == JobStreamState::Beginning ||
            job_stream_.state() == JobStreamState::Streaming ||
            job_stream_.state() == JobStreamState::PausedByHold) {
            job_stream_.note_run_started();
        }
        changed = machine_fsm_.handle_event(MachineEvent::GrblCycle);
    } else if (std::strcmp(text, "HOLD") == 0) {
        clear_command_in_flight();
        job_stream_.note_hold();
        if (std::strcmp(substate, "COMPLETE") == 0 ||
            std::strcmp(substate, "C") == 0 ||
            std::strcmp(substate, "1") == 0) {
            changed = machine_fsm_.handle_event(MachineEvent::GrblHoldComplete);
        } else {
            changed = machine_fsm_.handle_event(MachineEvent::GrblHoldPending);
        }
    } else if (std::strcmp(text, "JOG") == 0) {
        clear_command_in_flight();
        changed = machine_fsm_.handle_event(MachineEvent::GrblJog);
    } else if (std::strcmp(text, "ALARM") == 0 || std::strcmp(text, "FAULT") == 0) {
        clear_command_in_flight();
        job_stream_.note_fault();
        changed = machine_fsm_.handle_event(MachineEvent::GrblAlarm);
    } else if (std::strcmp(text, "ESTOP") == 0) {
        clear_command_in_flight();
        job_stream_.note_fault();
        changed = machine_fsm_.handle_event(MachineEvent::GrblEstop);
    } else if (std::strcmp(text, "DOOR") == 0) {
        clear_command_in_flight();
        job_stream_.note_hold();
        changed = machine_fsm_.handle_event(MachineEvent::GrblDoor);
    } else if (std::strcmp(text, "TOOL_CHANGE") == 0) {
        clear_command_in_flight();
        job_stream_.note_hold();
        changed = machine_fsm_.handle_event(MachineEvent::GrblToolChange);
    } else if (std::strcmp(text, "SLEEP") == 0) {
        clear_command_in_flight();
        changed = machine_fsm_.handle_event(MachineEvent::GrblSleep);
    }

    result.machine_changed = result.machine_changed || changed;
    return result;
}
