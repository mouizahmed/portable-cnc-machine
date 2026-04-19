#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
    #include "driver.h"
    #include "grbl/grbl.h"
    #include "grbl/protocol.h"
    #include "grbl/state_machine.h"
    #include "grbl/stream_file.h"
    #include "grbl/system.h"
    #include "grbl/task.h"
    #include "grbl/vfs.h"
}

#ifndef PICO_UART_BAUD
#define PICO_UART_BAUD 115200
#endif

#ifndef PICO_UART_POLL_MS
#define PICO_UART_POLL_MS 5
#endif

#ifndef PICO_UART_LINE_LENGTH
#define PICO_UART_LINE_LENGTH 192
#endif

#ifndef PICO_JOB_PATH
#define PICO_JOB_PATH LITTLEFS_MOUNT_DIR "/pico_job.nc"
#endif

namespace {

enum class PicoMachineState : uint8_t {
    Idle,
    Running,
    Hold,
    Alarm,
    Estop
};

enum class PicoJobState : uint8_t {
    NoFileSelected,
    FileSelected,
    Running
};

struct PicoFrameworkContext {
    char rx_line[PICO_UART_LINE_LENGTH] = {0};
    size_t rx_length = 0;
    bool linked = false;
    bool upload_active = false;
    bool upload_ready = false;
    bool run_active = false;
    int selected_index = -1;
    uint32_t expected_lines = 0;
    uint32_t received_lines = 0;
    uint32_t last_pos_report_ms = 0;
    PicoMachineState last_machine_state = PicoMachineState::Idle;
    PicoJobState job_state = PicoJobState::NoFileSelected;
    vfs_file_t *upload_file = nullptr;
    vfs_file_t *run_file = nullptr;
};

PicoFrameworkContext pico_ctx;

HardwareSerial &pico_uart ()
{
    return Serial1;
}

const char *machine_state_name (PicoMachineState state)
{
    switch(state) {
        case PicoMachineState::Idle: return "IDLE";
        case PicoMachineState::Running: return "RUNNING";
        case PicoMachineState::Hold: return "HOLD";
        case PicoMachineState::Alarm: return "ALARM";
        case PicoMachineState::Estop: return "ESTOP";
    }

    return "ALARM";
}

const char *job_state_name (PicoJobState state)
{
    switch(state) {
        case PicoJobState::NoFileSelected: return "NO_FILE_SELECTED";
        case PicoJobState::FileSelected: return "FILE_SELECTED";
        case PicoJobState::Running: return "RUNNING";
    }

    return "NO_FILE_SELECTED";
}

void uart_send_line (const char *line)
{
    pico_uart().println(line);
}

void uart_sendf (const char *fmt, ...)
{
    char buffer[220];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    uart_send_line(buffer);
}

void send_ack (int seq, const char *fmt = nullptr, ...)
{
    char suffix[128] = {0};

    if(fmt != nullptr) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(suffix, sizeof(suffix), fmt, args);
        va_end(args);
    }

    if(suffix[0] != '\0')
        uart_sendf("@ACK SEQ=%d %s", seq, suffix);
    else
        uart_sendf("@ACK SEQ=%d", seq);
}

void send_error (int seq, const char *code)
{
    if(seq >= 0)
        uart_sendf("@ERR SEQ=%d CODE=%s", seq, code);
    else
        uart_sendf("@ERR CODE=%s", code);
}

void trim_line (char *line)
{
    size_t length = strlen(line);

    while(length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n' || isspace((unsigned char)line[length - 1])))
        line[--length] = '\0';

    size_t start = 0;
    while(line[start] != '\0' && isspace((unsigned char)line[start]))
        start++;

    if(start > 0)
        memmove(line, line + start, strlen(line + start) + 1);
}

bool is_blank_line (const char *line)
{
    while(*line != '\0') {
        if(!isspace((unsigned char)*line))
            return false;
        line++;
    }

    return true;
}

bool extract_token (const char *line, const char *key, char *out, size_t size)
{
    if(line == nullptr || key == nullptr || out == nullptr || size == 0)
        return false;

    const char *start = strstr(line, key);
    if(start == nullptr)
        return false;

    start += strlen(key);

    size_t index = 0;
    while(start[index] != '\0' && start[index] != ' ' && index + 1 < size)
        out[index++] = start[index];

    out[index] = '\0';

    return index > 0;
}

bool extract_int (const char *line, const char *key, int *value)
{
    char buffer[24];
    if(value == nullptr || !extract_token(line, key, buffer, sizeof(buffer)))
        return false;

    *value = atoi(buffer);
    return true;
}

bool extract_float (const char *line, const char *key, float *value)
{
    char buffer[32];
    if(value == nullptr || !extract_token(line, key, buffer, sizeof(buffer)))
        return false;

    *value = strtof(buffer, nullptr);
    return true;
}

PicoMachineState current_machine_state ()
{
    const sys_state_t state = state_get();

    if(state & STATE_ESTOP)
        return PicoMachineState::Estop;
    if(state & STATE_ALARM)
        return PicoMachineState::Alarm;
    if(state & (STATE_HOLD | STATE_TOOL_CHANGE))
        return PicoMachineState::Hold;
    if(state & (STATE_CYCLE | STATE_JOG | STATE_HOMING | STATE_SLEEP))
        return PicoMachineState::Running;

    return PicoMachineState::Idle;
}

void report_job_state ()
{
    if(pico_ctx.selected_index >= 0)
        uart_sendf("@EVT JOB STATE=%s INDEX=%d", job_state_name(pico_ctx.job_state), pico_ctx.selected_index);
    else
        uart_sendf("@EVT JOB STATE=%s", job_state_name(pico_ctx.job_state));
}

void set_job_state (PicoJobState state)
{
    if(pico_ctx.job_state != state) {
        pico_ctx.job_state = state;
        report_job_state();
    }
}

void report_machine_state ()
{
    pico_ctx.last_machine_state = current_machine_state();
    uart_sendf("@EVT MACHINE STATE=%s", machine_state_name(pico_ctx.last_machine_state));
}

void report_position ()
{
    float mpos[N_AXIS] = {0.0f};
    system_convert_array_steps_to_mpos(mpos, sys.position);
    uart_sendf("@EVT POS X=%.3f Y=%.3f Z=%.3f",
               (double)mpos[0],
               (double)(N_AXIS > 1 ? mpos[1] : 0.0f),
               (double)(N_AXIS > 2 ? mpos[2] : 0.0f));
    pico_ctx.last_pos_report_ms = millis();
}

void close_upload_file ()
{
    if(pico_ctx.upload_file != nullptr) {
        vfs_close(pico_ctx.upload_file);
        pico_ctx.upload_file = nullptr;
    }
}

void close_run_file ()
{
    if(pico_ctx.run_file != nullptr) {
        stream_redirect_close(pico_ctx.run_file);
        pico_ctx.run_file = nullptr;
    }
}

void reset_upload_state ()
{
    close_upload_file();
    pico_ctx.upload_active = false;
    pico_ctx.upload_ready = false;
    pico_ctx.expected_lines = 0;
    pico_ctx.received_lines = 0;
}

void stop_run (bool preserve_uploaded_job)
{
    close_run_file();
    pico_ctx.run_active = false;

    if(!preserve_uploaded_job) {
        pico_ctx.upload_ready = false;
        pico_ctx.selected_index = -1;
        set_job_state(PicoJobState::NoFileSelected);
    } else if(pico_ctx.selected_index >= 0) {
        set_job_state(PicoJobState::FileSelected);
    }
}

bool queue_jog_command (char axis, float dist_mm, int feed)
{
    char command[64];
    snprintf(command, sizeof(command), "$J=G91 G21 %c%.3f F%d", axis, (double)dist_mm, feed);
    return protocol_enqueue_gcode(command);
}

status_code_t on_run_status (status_code_t status)
{
    if(status != Status_OK && status != Status_Unhandled)
        uart_sendf("@ERR CODE=RUN_STATUS_%d", status);

    return status;
}

status_code_t on_run_end (vfs_file_t *file, status_code_t status)
{
    (void)file;

    pico_ctx.run_active = false;
    pico_ctx.run_file = nullptr;
    uart_sendf("@EVT JOB_PROGRESS SENT=%lu TOTAL=%lu",
               (unsigned long)pico_ctx.received_lines,
               (unsigned long)pico_ctx.received_lines);
    stop_run(true);

    if(status != Status_OK && status != Status_Unhandled)
        uart_sendf("@ERR CODE=RUN_END_%d", status);

    return status;
}

void report_state_changes ()
{
    const PicoMachineState state = current_machine_state();
    if(state != pico_ctx.last_machine_state)
        report_machine_state();

    const uint32_t now = millis();
    const uint32_t interval = state == PicoMachineState::Idle ? 1000 : 200;
    if(now - pico_ctx.last_pos_report_ms >= interval)
        report_position();
}

void service_job_execution ()
{
    if(!pico_ctx.run_active)
        return;

    const PicoMachineState machine_state = current_machine_state();
    if(machine_state == PicoMachineState::Alarm || machine_state == PicoMachineState::Estop) {
        stop_run(true);
        return;
    }
}

void handle_upload_line (char *line)
{
    if(!pico_ctx.upload_active || pico_ctx.upload_file == nullptr) {
        send_error(-1, "UNEXPECTED_GCODE");
        return;
    }

    trim_line(line);
    if(is_blank_line(line))
        return;

    vfs_puts(line, pico_ctx.upload_file);
    vfs_puts("\n", pico_ctx.upload_file);
    pico_ctx.received_lines++;
}

void handle_command (char *line)
{
    int seq = -1;
    char op[32] = {0};

    extract_int(line, "SEQ=", &seq);

    if(strncmp(line, "@HELLO", 6) == 0) {
        pico_ctx.linked = true;
        uart_send_line("@HELLO PROTO=1 CAPS=STATUS,JOG,MACHINING");
        report_machine_state();
        report_job_state();
        report_position();
        return;
    }

    if(strncmp(line, "@PING", 5) == 0) {
        send_ack(seq, "PONG=1");
        return;
    }

    if(strncmp(line, "@CMD", 4) != 0 || !extract_token(line, "OP=", op, sizeof(op))) {
        send_error(seq, "UNKNOWN_COMMAND");
        return;
    }

    if(strcmp(op, "JOB_SELECT") == 0) {
        int index = -1;
        if(!extract_int(line, "INDEX=", &index)) {
            send_error(seq, "BAD_INDEX");
            return;
        }

        pico_ctx.selected_index = index;
        if(!pico_ctx.run_active)
            set_job_state(PicoJobState::FileSelected);

        send_ack(seq);
        return;
    }

    if(strcmp(op, "JOB_BEGIN") == 0) {
        int index = -1;
        int lines_expected = 0;
        reset_upload_state();
        close_run_file();
        vfs_unlink(PICO_JOB_PATH);

        extract_int(line, "INDEX=", &index);
        extract_int(line, "LINES=", &lines_expected);
        pico_ctx.selected_index = index;
        pico_ctx.expected_lines = lines_expected > 0 ? (uint32_t)lines_expected : 0;
        pico_ctx.upload_file = vfs_open(PICO_JOB_PATH, "w");

        if(pico_ctx.upload_file == nullptr) {
            send_error(seq, "OPEN_FAILED");
            return;
        }

        pico_ctx.upload_active = true;
        pico_ctx.upload_ready = false;
        pico_ctx.received_lines = 0;
        send_ack(seq);
        return;
    }

    if(strcmp(op, "JOB_END") == 0) {
        if(!pico_ctx.upload_active || pico_ctx.upload_file == nullptr) {
            send_error(seq, "NO_UPLOAD");
            return;
        }

        close_upload_file();
        pico_ctx.upload_active = false;
        pico_ctx.upload_ready = pico_ctx.received_lines > 0;
        if(!pico_ctx.upload_ready) {
            send_error(seq, "EMPTY_JOB");
            return;
        }

        set_job_state(PicoJobState::FileSelected);
        send_ack(seq, "LINES=%lu READY=1", (unsigned long)pico_ctx.received_lines);
        return;
    }

    if(strcmp(op, "RUN") == 0) {
        if(!pico_ctx.upload_ready) {
            send_error(seq, "NOT_READY");
            return;
        }

        close_run_file();
        char path[] = PICO_JOB_PATH;
        pico_ctx.run_file = stream_redirect_read(path, on_run_status, on_run_end);
        if(pico_ctx.run_file == nullptr) {
            send_error(seq, "OPEN_FAILED");
            return;
        }

        pico_ctx.run_active = true;
        set_job_state(PicoJobState::Running);
        uart_sendf("@EVT JOB_PROGRESS SENT=0 TOTAL=%lu", (unsigned long)pico_ctx.received_lines);
        send_ack(seq);
        return;
    }

    if(strcmp(op, "HOLD") == 0) {
        protocol_enqueue_realtime_command(CMD_FEED_HOLD);
        send_ack(seq);
        return;
    }

    if(strcmp(op, "RESUME") == 0) {
        protocol_enqueue_realtime_command(CMD_CYCLE_START);
        send_ack(seq);
        return;
    }

    if(strcmp(op, "ABORT") == 0) {
        protocol_enqueue_realtime_command(CMD_RESET);
        stop_run(true);
        send_ack(seq);
        return;
    }

    if(strcmp(op, "JOG") == 0) {
        char axis_text[4] = {0};
        float dist_mm = 0.0f;
        int feed = 0;
        if(!extract_token(line, "AXIS=", axis_text, sizeof(axis_text)) ||
           !extract_float(line, "DIST_MM=", &dist_mm) ||
           !extract_int(line, "FEED=", &feed) ||
           !queue_jog_command(axis_text[0], dist_mm, feed)) {
            send_error(seq, "BAD_JOG");
            return;
        }

        send_ack(seq);
        return;
    }

    if(strcmp(op, "HOME_ALL") == 0) {
        char command[] = "$H";
        if(system_execute_line(command) != Status_OK) {
            send_error(seq, "HOME_FAILED");
            return;
        }

        send_ack(seq);
        return;
    }

    if(strcmp(op, "ZERO_ALL") == 0) {
        char command[] = "G10 L20 P1 X0 Y0 Z0";
        if(!protocol_enqueue_gcode(command)) {
            send_error(seq, "ZERO_FAILED");
            return;
        }

        send_ack(seq);
        return;
    }

    send_error(seq, "UNKNOWN_OP");
}

void handle_input_line (char *line)
{
    trim_line(line);

    if(is_blank_line(line))
        return;

    if(line[0] == '@')
        handle_command(line);
    else
        handle_upload_line(line);
}

void service_uart_rx ()
{
    while(pico_uart().available() > 0) {
        char c = (char)pico_uart().read();

        if(c == '\r')
            continue;

        if(c == '\n') {
            pico_ctx.rx_line[pico_ctx.rx_length] = '\0';
            handle_input_line(pico_ctx.rx_line);
            pico_ctx.rx_length = 0;
            pico_ctx.rx_line[0] = '\0';
            continue;
        }

        if(pico_ctx.rx_length + 1 >= sizeof(pico_ctx.rx_line)) {
            pico_ctx.rx_length = 0;
            pico_ctx.rx_line[0] = '\0';
            send_error(-1, "RX_LINE_TOO_LONG");
            continue;
        }

        pico_ctx.rx_line[pico_ctx.rx_length++] = c;
    }
}

void pico_framework_service_task (void *data)
{
    (void)data;

    service_uart_rx();
    service_job_execution();
    report_state_changes();
    task_add_delayed(pico_framework_service_task, NULL, PICO_UART_POLL_MS);
}

void pico_framework_startup_task (void *data)
{
    (void)data;

    pico_uart().begin(PICO_UART_BAUD);
    uart_send_line("@HELLO PROTO=1 CAPS=STATUS,JOG,MACHINING");
    report_machine_state();
    report_job_state();
    report_position();
    task_add_delayed(pico_framework_service_task, NULL, PICO_UART_POLL_MS);
}

} // namespace

extern "C" void my_plugin_init (void)
{
    task_run_on_startup(pico_framework_startup_task, NULL);
}
