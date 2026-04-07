#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern "C" {
    #include "driver.h"
    #include "grbl/grbl.h"
    #include "grbl/protocol.h"
    #include "grbl/system.h"
    #include "grbl/state_machine.h"
    #include "grbl/task.h"
}

#ifndef PICO_UART_BAUD
#define PICO_UART_BAUD 115200
#endif

#ifndef PICO_UART_POLL_MS
#define PICO_UART_POLL_MS 5
#endif

#ifndef PICO_JOB_BUFFER_LINES
#define PICO_JOB_BUFFER_LINES 64
#endif

#ifndef PICO_JOB_LINE_LENGTH
#define PICO_JOB_LINE_LENGTH 96
#endif

#ifndef PICO_POT0_PIN
#define PICO_POT0_PIN -1
#endif

#ifndef PICO_POT1_PIN
#define PICO_POT1_PIN -1
#endif

#ifndef PICO_Z_PROBE_PIN
#define PICO_Z_PROBE_PIN -1
#endif

namespace {

enum class PicoMachineState : uint8_t {
    Idle,
    Loading,
    Ready,
    Running,
    Paused,
    Complete,
    Error
};

struct PicoFrameworkContext {
    PicoMachineState state = PicoMachineState::Idle;
    char rx_line[PICO_JOB_LINE_LENGTH] = {0};
    size_t rx_length = 0;
    char job[PICO_JOB_BUFFER_LINES][PICO_JOB_LINE_LENGTH] = {{0}};
    size_t line_count = 0;
    size_t next_to_send = 0;
    bool announced_running = false;
    bool last_enqueue_busy = false;
};

PicoFrameworkContext pico_ctx;

HardwareSerial &pico_uart ()
{
    return Serial1;
}

const char *state_name (PicoMachineState state)
{
    switch(state) {
        case PicoMachineState::Idle: return "IDLE";
        case PicoMachineState::Loading: return "LOADING";
        case PicoMachineState::Ready: return "READY";
        case PicoMachineState::Running: return "RUNNING";
        case PicoMachineState::Paused: return "PAUSED";
        case PicoMachineState::Complete: return "COMPLETE";
        case PicoMachineState::Error: return "ERROR";
    }

    return "UNKNOWN";
}

void uart_send_line (const char *line)
{
    pico_uart().println(line);
}

void uart_sendf (const char *fmt, ...)
{
    char buffer[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    uart_send_line(buffer);
}

void reset_job_buffer ()
{
    pico_ctx.line_count = 0;
    pico_ctx.next_to_send = 0;
    pico_ctx.announced_running = false;
    pico_ctx.last_enqueue_busy = false;
    memset(pico_ctx.job, 0, sizeof(pico_ctx.job));
}

void set_state (PicoMachineState new_state)
{
    if(pico_ctx.state != new_state) {
        pico_ctx.state = new_state;
        uart_sendf("@STATE %s", state_name(new_state));
    }
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

void report_sensor_snapshot ()
{
    int x_limit = digitalRead(X_LIMIT_PIN);
    int y_limit = digitalRead(Y_LIMIT_PIN);
    int z_limit = digitalRead(Z_LIMIT_PIN);

#if PICO_POT0_PIN >= 0
    int pot0 = analogRead(PICO_POT0_PIN);
#else
    int pot0 = -1;
#endif

#if PICO_POT1_PIN >= 0
    int pot1 = analogRead(PICO_POT1_PIN);
#else
    int pot1 = -1;
#endif

#if PICO_Z_PROBE_PIN >= 0
    int probe = digitalRead(PICO_Z_PROBE_PIN);
#else
    int probe = -1;
#endif

    uart_sendf("@SENSORS LIMIT_X=%d LIMIT_Y=%d LIMIT_Z=%d PROBE=%d POT0=%d POT1=%d",
               x_limit, y_limit, z_limit, probe, pot0, pot1);
}

void report_framework_status ()
{
    sys_state_t grbl_state = state_get();
    uart_sendf("@STATUS FRAMEWORK=%s GRBL=%u BUFFERED=%u NEXT=%u",
               state_name(pico_ctx.state),
               (unsigned int)grbl_state,
               (unsigned int)pico_ctx.line_count,
               (unsigned int)pico_ctx.next_to_send);
}

bool store_gcode_line (char *line)
{
    if(pico_ctx.state != PicoMachineState::Loading) {
        uart_send_line("@ERROR INVALID_STATE_FOR_GCODE");
        set_state(PicoMachineState::Error);
        return false;
    }

    if(strlen(line) >= PICO_JOB_LINE_LENGTH) {
        uart_send_line("@ERROR GCODE_LINE_TOO_LONG");
        set_state(PicoMachineState::Error);
        return false;
    }

    if(pico_ctx.line_count >= PICO_JOB_BUFFER_LINES) {
        uart_send_line("@ERROR JOB_BUFFER_FULL");
        set_state(PicoMachineState::Error);
        return false;
    }

    strncpy(pico_ctx.job[pico_ctx.line_count], line, PICO_JOB_LINE_LENGTH - 1);
    pico_ctx.job[pico_ctx.line_count][PICO_JOB_LINE_LENGTH - 1] = '\0';
    pico_ctx.line_count++;
    uart_sendf("@OK LINE %u", (unsigned int)pico_ctx.line_count);

    return true;
}

void handle_control_command (char *line)
{
    if(strcmp(line, "@PING") == 0) {
        uart_send_line("@OK PONG");
        return;
    }

    if(strcmp(line, "@STATUS") == 0) {
        report_framework_status();
        return;
    }

    if(strcmp(line, "@READ_SENSORS") == 0) {
        report_sensor_snapshot();
        return;
    }

    if(strcmp(line, "@BEGIN_JOB") == 0) {
        reset_job_buffer();
        set_state(PicoMachineState::Loading);
        uart_send_line("@OK BEGIN_JOB");
        return;
    }

    if(strcmp(line, "@END_JOB") == 0) {
        if(pico_ctx.state != PicoMachineState::Loading) {
            uart_send_line("@ERROR INVALID_STATE_END_JOB");
            set_state(PicoMachineState::Error);
            return;
        }

        if(pico_ctx.line_count == 0) {
            uart_send_line("@ERROR EMPTY_JOB");
            set_state(PicoMachineState::Error);
            return;
        }

        pico_ctx.next_to_send = 0;
        set_state(PicoMachineState::Ready);
        uart_sendf("@OK END_JOB LINES=%u", (unsigned int)pico_ctx.line_count);
        return;
    }

    if(strcmp(line, "@CLEAR_JOB") == 0) {
        reset_job_buffer();
        set_state(PicoMachineState::Idle);
        uart_send_line("@OK CLEAR_JOB");
        return;
    }

    if(strcmp(line, "@STARTMACHINING") == 0 || strcmp(line, "@START_MACHINE") == 0) {
        if(pico_ctx.state != PicoMachineState::Ready && pico_ctx.state != PicoMachineState::Complete) {
            uart_send_line("@ERROR INVALID_STATE_START");
            set_state(PicoMachineState::Error);
            return;
        }

        if(pico_ctx.line_count == 0) {
            uart_send_line("@ERROR EMPTY_JOB");
            set_state(PicoMachineState::Error);
            return;
        }

        pico_ctx.next_to_send = 0;
        pico_ctx.announced_running = false;
        pico_ctx.last_enqueue_busy = false;
        set_state(PicoMachineState::Running);
        uart_send_line("@OK STARTMACHINING");
        return;
    }

    if(strcmp(line, "@PAUSE") == 0) {
        protocol_enqueue_realtime_command(CMD_FEED_HOLD);
        set_state(PicoMachineState::Paused);
        uart_send_line("@OK PAUSE");
        return;
    }

    if(strcmp(line, "@RESUME") == 0) {
        protocol_enqueue_realtime_command(CMD_CYCLE_START);
        if(pico_ctx.state == PicoMachineState::Paused)
            set_state(PicoMachineState::Running);
        uart_send_line("@OK RESUME");
        return;
    }

    if(strcmp(line, "@ABORT") == 0) {
        protocol_enqueue_realtime_command(CMD_RESET);
        reset_job_buffer();
        set_state(PicoMachineState::Idle);
        uart_send_line("@OK ABORT");
        return;
    }

    uart_send_line("@ERROR UNKNOWN_COMMAND");
    set_state(PicoMachineState::Error);
}

void handle_input_line (char *line)
{
    trim_line(line);

    if(is_blank_line(line))
        return;

    if(line[0] == '@')
        handle_control_command(line);
    else
        store_gcode_line(line);
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

        if(pico_ctx.rx_length >= (PICO_JOB_LINE_LENGTH - 1)) {
            pico_ctx.rx_length = 0;
            pico_ctx.rx_line[0] = '\0';
            uart_send_line("@ERROR RX_LINE_TOO_LONG");
            set_state(PicoMachineState::Error);
            continue;
        }

        pico_ctx.rx_line[pico_ctx.rx_length++] = c;
    }
}

void service_job_execution ()
{
    if(pico_ctx.state != PicoMachineState::Running)
        return;

    if(state_get() & (STATE_ALARM | STATE_ESTOP)) {
        uart_send_line("@ERROR GRBL_NOT_READY");
        set_state(PicoMachineState::Error);
        return;
    }

    if(!pico_ctx.announced_running) {
        uart_send_line("@STATUS RUNNING");
        pico_ctx.announced_running = true;
    }

    if(pico_ctx.next_to_send < pico_ctx.line_count) {
        if(protocol_enqueue_gcode(pico_ctx.job[pico_ctx.next_to_send])) {
            pico_ctx.next_to_send++;
            pico_ctx.last_enqueue_busy = false;
            uart_sendf("@OK EXEC %u/%u",
                       (unsigned int)pico_ctx.next_to_send,
                       (unsigned int)pico_ctx.line_count);
        } else if(!pico_ctx.last_enqueue_busy) {
            pico_ctx.last_enqueue_busy = true;
            uart_send_line("@WAIT GRBL_BUSY");
        }

        return;
    }

    if(state_get() == STATE_IDLE) {
        set_state(PicoMachineState::Complete);
        uart_send_line("@STATUS COMPLETE");
    }
}

void pico_framework_service_task (void *data)
{
    (void)data;

    service_uart_rx();
    service_job_execution();
    task_add_delayed(pico_framework_service_task, NULL, PICO_UART_POLL_MS);
}

void pico_framework_startup_task (void *data)
{
    (void)data;

    pico_uart().begin(PICO_UART_BAUD);
    pico_uart().println("@BOOT TEENSY_READY");
    reset_job_buffer();
    set_state(PicoMachineState::Idle);
    task_add_delayed(pico_framework_service_task, NULL, PICO_UART_POLL_MS);
}

} // namespace

extern "C" void my_plugin_init (void)
{
#if PICO_POT0_PIN >= 0
    pinMode(PICO_POT0_PIN, INPUT);
#endif
#if PICO_POT1_PIN >= 0
    pinMode(PICO_POT1_PIN, INPUT);
#endif
#if PICO_Z_PROBE_PIN >= 0
    pinMode(PICO_Z_PROBE_PIN, INPUT_PULLUP);
#endif

    task_run_on_startup(pico_framework_startup_task, NULL);
}
