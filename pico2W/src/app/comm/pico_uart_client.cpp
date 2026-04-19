#include "app/comm/pico_uart_client.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "ff.h"
}

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

bool read_file_line(FIL& file, char* buffer, std::size_t size) {
    if (buffer == nullptr || size == 0) {
        return false;
    }

    std::size_t index = 0;
    while (index + 1 < size) {
        char c = '\0';
        UINT bytes_read = 0;
        const FRESULT fr = f_read(&file, &c, 1, &bytes_read);
        if (fr != FR_OK || bytes_read == 0) {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        buffer[index++] = c;
    }

    buffer[index] = '\0';
    return index > 0;
}
}  // namespace

PicoUartClient::PicoUartClient(MachineFsm& machine_fsm,
                               JogStateMachine& jog_state_machine,
                               JobStateMachine& job_state_machine,
                               const StorageService& storage_service)
    : machine_fsm_(machine_fsm),
      jog_state_machine_(jog_state_machine),
      job_state_machine_(job_state_machine),
      storage_service_(storage_service) {
    init_transport();
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    last_rx_ms_ = now;
    last_probe_ms_ = 0;
}

PicoUartPollResult PicoUartClient::poll() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    PicoUartPollResult result = process_input(now);

    const uint32_t quiet_for = now - last_rx_ms_;
    if (quiet_for < kSilentLinkThresholdMs) {
        return result;
    }

    if (now - last_probe_ms_ < kProbeIntervalMs) {
        return result;
    }

    if (send_probe()) {
        last_probe_ms_ = now;
        if (missed_probes_ < kMaxMissedProbes) {
            ++missed_probes_;
        }
    }

    if (linked_ && missed_probes_ >= kMaxMissedProbes) {
        merge(result, mark_link_disconnected());
    }

    return result;
}

bool PicoUartClient::select_file(int16_t index) {
    if (index < 0 || index >= static_cast<int16_t>(job_state_machine_.count())) {
        return false;
    }

    const FileEntry& entry = job_state_machine_.entry(static_cast<std::size_t>(index));
    return send_command("@CMD SEQ=%lu OP=JOB_SELECT INDEX=%d NAME=%s",
                        static_cast<unsigned long>(next_seq()),
                        static_cast<int>(index),
                        entry.name);
}

bool PicoUartClient::upload_and_run_loaded_job() {
    const FileEntry* entry = job_state_machine_.loaded_entry();
    const int16_t index = job_state_machine_.loaded_index();
    if (entry == nullptr || index < 0 || !storage_service_.is_mounted()) {
        return false;
    }

    if (!upload_job_file(*entry, index)) {
        return false;
    }

    return send_command("@CMD SEQ=%lu OP=RUN", static_cast<unsigned long>(next_seq()));
}

bool PicoUartClient::hold() {
    return send_command("@CMD SEQ=%lu OP=HOLD", static_cast<unsigned long>(next_seq()));
}

bool PicoUartClient::resume() {
    return send_command("@CMD SEQ=%lu OP=RESUME", static_cast<unsigned long>(next_seq()));
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

    return send_command("@CMD SEQ=%lu OP=JOG AXIS=%c DIST_MM=%.3f FEED=%d",
                        static_cast<unsigned long>(next_seq()),
                        axis,
                        static_cast<double>(distance),
                        static_cast<int>(jog_state_machine_.feed_rate_mm_min()));
}

bool PicoUartClient::home_all() {
    return send_command("@CMD SEQ=%lu OP=HOME_ALL", static_cast<unsigned long>(next_seq()));
}

bool PicoUartClient::zero_all() {
    return send_command("@CMD SEQ=%lu OP=ZERO_ALL", static_cast<unsigned long>(next_seq()));
}

const char* PicoUartClient::link_status() const {
    return linked_ ? "UART" : "DISC";
}

void PicoUartClient::init_transport() {
    if (transport_initialized_) {
        return;
    }

    uart_init(uart1, MOTION_UART_BAUD);
    gpio_set_function(PIN_MOTION_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_MOTION_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(uart1, false, false);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, true);
    transport_initialized_ = true;
}

bool PicoUartClient::send_probe() {
    // Keep compatibility with the existing @HELLO/@EVT flow while preferring
    // a simple ping probe for quiet-link liveness.
    const bool ping_ok = send_ping();
    const bool hello_ok = send_hello();
    return ping_ok || hello_ok;
}

bool PicoUartClient::send_ping() {
    return send_raw_line("@PING");
}

bool PicoUartClient::send_hello() {
    return send_raw_line("@HELLO PROTO=1 CAPS=STATUS,JOG,MACHINING");
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
    uart_write_blocking(uart1, reinterpret_cast<const uint8_t*>(line), line_len);
    uart_write_blocking(uart1, reinterpret_cast<const uint8_t*>("\n"), 1);
    return true;
}

uint32_t PicoUartClient::next_seq() {
    return next_seq_++;
}

PicoUartPollResult PicoUartClient::process_input(uint32_t now) {
    PicoUartPollResult result{};

    while (uart_is_readable(uart1)) {
        const int c = uart_getc(uart1);
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
    if (line == nullptr || *line == '\0' || line[0] != '@') {
        return result;
    }

    if (starts_with(line, "@HELLO") || starts_with(line, "@PONG")) {
        mark_valid_traffic(now);
        return mark_link_connected(now);
    }

    if (starts_with(line, "@BOOT")) {
        return handle_boot_line(line, now);
    }

    if (starts_with(line, "@GRBL_STATE")) {
        return handle_grbl_state_line(line, now);
    }

    if (starts_with(line, "@EVT")) {
        return handle_event_line(line, now);
    }

    if (starts_with(line, "@ERR")) {
        return handle_error_line(now);
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

PicoUartPollResult PicoUartClient::handle_event_line(const char* line, uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);

    char kind[24] = {};
    if (!extract_token(line, "@EVT ", kind, sizeof(kind))) {
        return result;
    }

    if (std::strcmp(kind, "MACHINE") == 0) {
        char state_text[24] = {};
        if (!extract_token(line, "STATE=", state_text, sizeof(state_text))) {
            return result;
        }
        merge(result, handle_machine_state(state_text, "", now));
        return result;
    }

    if (std::strcmp(kind, "JOB") == 0) {
        char state_text[24] = {};
        int index = -1;
        if (!extract_token(line, "STATE=", state_text, sizeof(state_text))) {
            return result;
        }
        extract_int(line, "INDEX=", index);
        merge(result, handle_job_state(state_text, index, now));
        return result;
    }

    if (std::strcmp(kind, "POS") == 0) {
        float x = jog_state_machine_.x();
        float y = jog_state_machine_.y();
        float z = jog_state_machine_.z();
        const bool ok_x = extract_float(line, "X=", x);
        const bool ok_y = extract_float(line, "Y=", y);
        const bool ok_z = extract_float(line, "Z=", z);
        if (!(ok_x && ok_y && ok_z)) {
            return result;
        }

        jog_state_machine_.set_position(x, y, z);
        result.position_changed = true;
        return result;
    }

    return result;
}

PicoUartPollResult PicoUartClient::handle_error_line(uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);
    const bool fault_changed = machine_fsm_.handle_event(MachineEvent::GrblAlarm);
    result.machine_changed = result.machine_changed || fault_changed;
    return result;
}

bool PicoUartClient::upload_job_file(const FileEntry& entry, int16_t index) {
    FIL file{};
    char path[64];
    std::snprintf(path, sizeof(path), "0:/%s", entry.name);
    if (f_open(&file, path, FA_READ) != FR_OK) {
        return false;
    }

    uint32_t line_count = 0;
    char line[kFileLineSize];
    while (read_file_line(file, line, sizeof(line))) {
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }
        ++line_count;
    }

    f_close(&file);
    if (line_count == 0) {
        return false;
    }

    send_command("@CMD SEQ=%lu OP=JOB_SELECT INDEX=%d NAME=%s",
                 static_cast<unsigned long>(next_seq()),
                 static_cast<int>(index),
                 entry.name);
    send_command("@CMD SEQ=%lu OP=JOB_BEGIN INDEX=%d LINES=%lu",
                 static_cast<unsigned long>(next_seq()),
                 static_cast<int>(index),
                 static_cast<unsigned long>(line_count));

    if (f_open(&file, path, FA_READ) != FR_OK) {
        return false;
    }

    while (read_file_line(file, line, sizeof(line))) {
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }

        send_raw_line(line);
        sleep_ms(2);
    }

    f_close(&file);
    send_command("@CMD SEQ=%lu OP=JOB_END", static_cast<unsigned long>(next_seq()));
    return true;
}

void PicoUartClient::mark_valid_traffic(uint32_t now) {
    last_rx_ms_ = now;
    missed_probes_ = 0;
}

PicoUartPollResult PicoUartClient::mark_link_connected(uint32_t now) {
    PicoUartPollResult result{};
    mark_valid_traffic(now);

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
    result.link_transition = MotionLinkTransition::Disconnected;
    result.machine_changed = machine_fsm_.handle_event(MachineEvent::TeensyDisconnected);
    return result;
}

void PicoUartClient::merge(PicoUartPollResult& into, const PicoUartPollResult& from) {
    into.machine_changed = into.machine_changed || from.machine_changed;
    into.job_changed = into.job_changed || from.job_changed;
    into.position_changed = into.position_changed || from.position_changed;
    if (from.link_transition != MotionLinkTransition::None) {
        into.link_transition = from.link_transition;
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

void PicoUartClient::trim_line(char* line) {
    if (line == nullptr) {
        return;
    }

    std::size_t length = std::strlen(line);
    while (length > 0 &&
           (line[length - 1] == '\r' ||
            line[length - 1] == '\n' ||
            line[length - 1] == ' ' ||
            line[length - 1] == '\t')) {
        line[--length] = '\0';
    }
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
        changed = machine_fsm_.handle_event(MachineEvent::GrblIdle);
    } else if (std::strcmp(text, "HOMING") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblHoming);
    } else if (std::strcmp(text, "CYCLE") == 0 || std::strcmp(text, "RUNNING") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblCycle);
    } else if (std::strcmp(text, "HOLD") == 0) {
        if (std::strcmp(substate, "COMPLETE") == 0 ||
            std::strcmp(substate, "C") == 0 ||
            std::strcmp(substate, "1") == 0) {
            changed = machine_fsm_.handle_event(MachineEvent::GrblHoldComplete);
        } else {
            changed = machine_fsm_.handle_event(MachineEvent::GrblHoldPending);
        }
    } else if (std::strcmp(text, "JOG") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblJog);
    } else if (std::strcmp(text, "ALARM") == 0 || std::strcmp(text, "FAULT") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblAlarm);
    } else if (std::strcmp(text, "ESTOP") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblEstop);
    } else if (std::strcmp(text, "DOOR") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblDoor);
    } else if (std::strcmp(text, "TOOL_CHANGE") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblToolChange);
    } else if (std::strcmp(text, "SLEEP") == 0) {
        changed = machine_fsm_.handle_event(MachineEvent::GrblSleep);
    }

    result.machine_changed = result.machine_changed || changed;
    return result;
}

PicoUartPollResult PicoUartClient::handle_job_state(const char* text, int index, uint32_t now) {
    PicoUartPollResult result = mark_link_connected(now);
    if (text == nullptr) {
        return result;
    }

    if (std::strcmp(text, "NO_FILE_SELECTED") == 0 || std::strcmp(text, "JOB_UNLOADED") == 0) {
        const bool job_changed = machine_fsm_.handle_event(MachineEvent::JobUnloaded);
        job_state_machine_.set_state(JobState::NoJobLoaded);
        result.machine_changed = result.machine_changed || job_changed;
        result.job_changed = true;
        return result;
    }

    if (std::strcmp(text, "FILE_SELECTED") == 0 || std::strcmp(text, "JOB_LOADED") == 0) {
        const bool job_changed = machine_fsm_.handle_event(MachineEvent::JobLoaded);
        job_state_machine_.set_state(JobState::JobLoaded, static_cast<int16_t>(index));
        result.machine_changed = result.machine_changed || job_changed;
        result.job_changed = true;
        return result;
    }

    if (std::strcmp(text, "RUNNING") == 0 || std::strcmp(text, "JOB_RUNNING") == 0) {
        const bool job_changed = machine_fsm_.handle_event(MachineEvent::JobLoaded);
        job_state_machine_.set_state(JobState::Running, static_cast<int16_t>(index));
        result.machine_changed = result.machine_changed || job_changed;
        result.job_changed = true;
        return result;
    }

    return result;
}
