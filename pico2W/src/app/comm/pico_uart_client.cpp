#include "app/comm/pico_uart_client.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "ff.h"
}

#include "pico/stdio.h"
#include "pico/stdlib.h"

namespace {
constexpr uint32_t kHandshakeRetryMs = 1000;

bool starts_with(const char* text, const char* prefix) {
    return text != nullptr && prefix != nullptr && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}
}  // namespace

PicoUartClient::PicoUartClient(MachineStateMachine& machine_state_machine,
                               JogStateMachine& jog_state_machine,
                               JobStateMachine& job_state_machine,
                               const StorageService& storage_service)
    : machine_state_machine_(machine_state_machine),
      jog_state_machine_(jog_state_machine),
      job_state_machine_(job_state_machine),
      storage_service_(storage_service) {}

bool PicoUartClient::poll() {
    static uint32_t last_hello_ms = 0;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    bool changed = false;

    if ((!hello_sent_ || !linked_) && (now - last_hello_ms) >= kHandshakeRetryMs) {
        send_hello();
        last_hello_ms = now;
    }

    changed |= process_input();
    return changed;
}

bool PicoUartClient::select_file(int16_t index) {
    if (index < 0 || index >= static_cast<int16_t>(job_state_machine_.count())) {
        return false;
    }

    const bool changed = job_state_machine_.handle_event(JobEvent::SelectFile, index);
    const FileEntry& entry = job_state_machine_.entry(static_cast<std::size_t>(index));
    send_command("@CMD SEQ=%lu OP=JOB_SELECT INDEX=%d NAME=%s",
                 static_cast<unsigned long>(next_seq()),
                 static_cast<int>(index),
                 entry.name);
    return changed;
}

bool PicoUartClient::upload_and_run_selected_job() {
    const FileEntry* entry = job_state_machine_.selected_entry();
    const int16_t index = job_state_machine_.selected_index();
    if (entry == nullptr || index < 0 || !storage_service_.is_mounted()) {
        return false;
    }

    if (!upload_job_file(*entry, index)) {
        return false;
    }

    send_command("@CMD SEQ=%lu OP=RUN", static_cast<unsigned long>(next_seq()));
    return true;
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

bool PicoUartClient::send_hello() {
    hello_sent_ = true;
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

    std::printf("%s\n", line);
    std::fflush(stdout);
    return true;
}

uint32_t PicoUartClient::next_seq() {
    return next_seq_++;
}

bool PicoUartClient::process_input() {
    bool changed = false;

    while (true) {
        const int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            break;
        }

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            rx_line_[rx_length_] = '\0';
            changed |= handle_line(rx_line_);
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

    return changed;
}

bool PicoUartClient::handle_line(const char* line) {
    if (line == nullptr || *line == '\0' || line[0] != '@') {
        return false;
    }

    if (starts_with(line, "@HELLO")) {
        linked_ = true;
        return true;
    }

    if (starts_with(line, "@EVT")) {
        return handle_event_line(line);
    }

    if (starts_with(line, "@ERR")) {
        return handle_error_line(line);
    }

    return false;
}

bool PicoUartClient::handle_event_line(const char* line) {
    char kind[24];
    if (!extract_token(line, "@EVT ", kind, sizeof(kind))) {
        return false;
    }

    if (std::strcmp(kind, "MACHINE") == 0) {
        char state_text[24];
        if (!extract_token(line, "STATE=", state_text, sizeof(state_text))) {
            return false;
        }

        machine_state_machine_.set_state(machine_state_from_text(state_text));
        return true;
    }

    if (std::strcmp(kind, "JOB") == 0) {
        char state_text[24];
        int index = -1;
        if (!extract_token(line, "STATE=", state_text, sizeof(state_text))) {
            return false;
        }
        extract_int(line, "INDEX=", index);
        job_state_machine_.set_state(job_state_from_text(state_text), static_cast<int16_t>(index));
        return true;
    }

    if (std::strcmp(kind, "POS") == 0) {
        float x = jog_state_machine_.x();
        float y = jog_state_machine_.y();
        float z = jog_state_machine_.z();
        const bool ok_x = extract_float(line, "X=", x);
        const bool ok_y = extract_float(line, "Y=", y);
        const bool ok_z = extract_float(line, "Z=", z);
        if (!(ok_x && ok_y && ok_z)) {
            return false;
        }

        jog_state_machine_.set_position(x, y, z);
        return true;
    }

    return false;
}

bool PicoUartClient::handle_error_line(const char* line) {
    (void)line;
    machine_state_machine_.set_state(MachineState::Alarm);
    return true;
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
    while (f_gets(line, sizeof(line), &file) != nullptr) {
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

    while (f_gets(line, sizeof(line), &file) != nullptr) {
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
    while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n' || line[length - 1] == ' ' || line[length - 1] == '\t')) {
        line[--length] = '\0';
    }
}

MachineState PicoUartClient::machine_state_from_text(const char* text) {
    if (text == nullptr) {
        return MachineState::Alarm;
    }

    if (std::strcmp(text, "IDLE") == 0) {
        return MachineState::Idle;
    }
    if (std::strcmp(text, "RUNNING") == 0) {
        return MachineState::Running;
    }
    if (std::strcmp(text, "HOLD") == 0) {
        return MachineState::Hold;
    }
    if (std::strcmp(text, "ESTOP") == 0) {
        return MachineState::Estop;
    }
    return MachineState::Alarm;
}

JobState PicoUartClient::job_state_from_text(const char* text) {
    if (text == nullptr) {
        return JobState::NoFileSelected;
    }

    if (std::strcmp(text, "FILE_SELECTED") == 0) {
        return JobState::FileSelected;
    }
    if (std::strcmp(text, "RUNNING") == 0) {
        return JobState::Running;
    }
    return JobState::NoFileSelected;
}
