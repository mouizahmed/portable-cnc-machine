#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
    #include "driver.h"
    #include "grbl/gcode.h"
    #include "grbl/grbl.h"
    #include "grbl/protocol.h"
    #include "grbl/state_machine.h"
    #include "grbl/system.h"
    #include "grbl/task.h"
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

#ifndef PICO_POS_REPORT_MS
#define PICO_POS_REPORT_MS 200
#endif

#ifndef PICO_UART_DEBUG_USB
#define PICO_UART_DEBUG_USB 1
#endif

namespace {

struct PicoBridgeContext {
    char rx_line[PICO_UART_LINE_LENGTH] = {0};
    size_t rx_length = 0;
    uint32_t last_pos_report_ms = 0;
    sys_state_t last_reported_state = (sys_state_t)0xFF;
    on_state_change_ptr previous_on_state_change = nullptr;
    on_realtime_report_ptr previous_on_realtime_report = nullptr;
};

PicoBridgeContext pico_ctx;

HardwareSerial& pico_uart()
{
    return Serial1;
}

void uart_send_line(const char* line)
{
    pico_uart().println(line);
#if PICO_UART_DEBUG_USB
    Serial.print("[PICO-TX] ");
    Serial.println(line);
#endif
}

void uart_sendf(const char* fmt, ...)
{
    char buffer[220];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    uart_send_line(buffer);
}

void trim_line(char* line)
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

bool is_blank_line(const char* line)
{
    while(*line != '\0') {
        if(!isspace((unsigned char)*line))
            return false;
        line++;
    }
    return true;
}

bool extract_token(const char* line, const char* key, char* out, size_t size)
{
    if(line == nullptr || key == nullptr || out == nullptr || size == 0)
        return false;

    const char* start = strstr(line, key);
    if(start == nullptr)
        return false;

    start += strlen(key);
    size_t index = 0;
    while(start[index] != '\0' && start[index] != ' ' && index + 1 < size)
        out[index++] = start[index];

    out[index] = '\0';
    return index > 0;
}

bool extract_float(const char* line, const char* key, float* value)
{
    char buffer[32];
    if(value == nullptr || !extract_token(line, key, buffer, sizeof(buffer)))
        return false;

    *value = strtof(buffer, nullptr);
    return true;
}

bool extract_int(const char* line, const char* key, int* value)
{
    char buffer[24];
    if(value == nullptr || !extract_token(line, key, buffer, sizeof(buffer)))
        return false;

    *value = atoi(buffer);
    return true;
}

const char* grbl_state_name(sys_state_t state)
{
    if(state & STATE_ESTOP)       return "ESTOP";
    if(state & STATE_ALARM)       return "ALARM";
    if(state & STATE_HOMING)      return "HOMING";
    if(state & STATE_CYCLE)       return "CYCLE";
    if(state & STATE_HOLD)        return "HOLD";
    if(state & STATE_JOG)         return "JOG";
    if(state & STATE_SAFETY_DOOR) return "DOOR";
    if(state & STATE_TOOL_CHANGE) return "TOOL_CHANGE";
    if(state & STATE_SLEEP)       return "SLEEP";
    return "IDLE";
}

void report_state(sys_state_t state)
{
    pico_ctx.last_reported_state = state;
    if(state & STATE_HOLD) {
        const uint8_t substate = sys.holding_state == Hold_Complete ? 1 : 0;
        uart_sendf("@GRBL_STATE HOLD SUBSTATE=%u", substate);
        return;
    }

    uart_sendf("@GRBL_STATE %s", grbl_state_name(state));
}

void report_position()
{
    float mpos[N_AXIS] = {0.0f};
    system_convert_array_steps_to_mpos(mpos, sys.position);

    float wpos[N_AXIS] = {0.0f};
    for(uint_fast8_t idx = 0; idx < N_AXIS; idx++) {
        float offset = gc_state.modal.g5x_offset.data.coord.values[idx] +
                       gc_state.g92_offset.coord.values[idx] +
                       gc_state.modal.tool_length_offset[idx];
        wpos[idx] = mpos[idx] - offset;
    }

    uart_sendf("@POS MX=%.3f MY=%.3f MZ=%.3f WX=%.3f WY=%.3f WZ=%.3f",
               (double)mpos[0],
               (double)(N_AXIS > 1 ? mpos[1] : 0.0f),
               (double)(N_AXIS > 2 ? mpos[2] : 0.0f),
               (double)wpos[0],
               (double)(N_AXIS > 1 ? wpos[1] : 0.0f),
               (double)(N_AXIS > 2 ? wpos[2] : 0.0f));
    pico_ctx.last_pos_report_ms = millis();
}

void maybe_report_position()
{
    const uint32_t now = millis();
    if(now - pico_ctx.last_pos_report_ms >= PICO_POS_REPORT_MS)
        report_position();
}

void send_status(status_code_t status)
{
    if(status == Status_OK)
        uart_send_line("ok");
    else
        uart_sendf("error:%u", (unsigned int)status);
}

void send_enqueue_status(bool ok)
{
    if(ok)
        uart_send_line("ok");
    else
        uart_send_line("error:1");
}

void enqueue_realtime(uint8_t command)
{
    protocol_enqueue_realtime_command(command);
    uart_send_line("ok");
}

void execute_system_line(const char* command)
{
    char buffer[96];
    strncpy(buffer, command, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    send_status(system_execute_line(buffer));
}

void enqueue_gcode_line(const char* command)
{
    char buffer[PICO_UART_LINE_LENGTH];
    strncpy(buffer, command, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    send_enqueue_status(protocol_enqueue_gcode(buffer));
}

void handle_jog(char* line)
{
    char axis_text[4] = {0};
    float dist_mm = 0.0f;
    int feed = 0;

    if(!extract_token(line, "AXIS=", axis_text, sizeof(axis_text)) ||
       !extract_float(line, "DIST=", &dist_mm) ||
       !extract_int(line, "FEED=", &feed)) {
        uart_send_line("error:2");
        return;
    }

    const char axis = (char)toupper((unsigned char)axis_text[0]);
    if(axis != 'X' && axis != 'Y' && axis != 'Z') {
        uart_send_line("error:2");
        return;
    }

    char command[80];
    snprintf(command, sizeof(command), "$J=G91 G21 %c%.3f F%d", axis, (double)dist_mm, feed);
    enqueue_gcode_line(command);
}

void handle_zero(char* line)
{
    char axis_text[8] = {0};
    if(!extract_token(line, "AXIS=", axis_text, sizeof(axis_text))) {
        uart_send_line("error:2");
        return;
    }

    for(char* p = axis_text; *p != '\0'; ++p)
        *p = (char)toupper((unsigned char)*p);

    char command[64] = {0};
    if(strcmp(axis_text, "ALL") == 0) {
        strcpy(command, "G10 L20 P1 X0 Y0 Z0");
    } else if(strcmp(axis_text, "X") == 0 || strcmp(axis_text, "Y") == 0 || strcmp(axis_text, "Z") == 0) {
        snprintf(command, sizeof(command), "G10 L20 P1 %s0", axis_text);
    } else {
        uart_send_line("error:2");
        return;
    }

    enqueue_gcode_line(command);
}

void handle_spindle_on(char* line)
{
    int rpm = 0;
    if(!extract_int(line, "RPM=", &rpm) || rpm <= 0) {
        uart_send_line("error:2");
        return;
    }

    char command[48];
    snprintf(command, sizeof(command), "M3 S%d", rpm);
    enqueue_gcode_line(command);
}

void handle_gcode(char* line)
{
    char* gcode = line + 6;
    while(*gcode == ' ')
        gcode++;

    if(*gcode == '\0') {
        uart_send_line("ok");
        return;
    }

    enqueue_gcode_line(gcode);
}

void handle_command(char* line)
{
    if(strcmp(line, "@PING") == 0) {
        uart_send_line("@PONG");
        return;
    }
    if(strcmp(line, "@HOME") == 0) {
        execute_system_line("$H");
        return;
    }
    if(strncmp(line, "@JOG ", 5) == 0) {
        handle_jog(line);
        return;
    }
    if(strcmp(line, "@JOG_CANCEL") == 0) {
        enqueue_realtime(CMD_JOG_CANCEL);
        return;
    }
    if(strncmp(line, "@GCODE", 6) == 0) {
        handle_gcode(line);
        return;
    }
    if(strcmp(line, "@RT_FEED_HOLD") == 0) {
        enqueue_realtime(CMD_FEED_HOLD);
        return;
    }
    if(strcmp(line, "@RT_CYCLE_START") == 0) {
        enqueue_realtime(CMD_CYCLE_START);
        return;
    }
    if(strcmp(line, "@RT_RESET") == 0) {
        enqueue_realtime(CMD_RESET);
        return;
    }
    if(strcmp(line, "@RT_ESTOP") == 0) {
        enqueue_realtime(CMD_RESET);
        return;
    }
    if(strcmp(line, "@UNLOCK") == 0) {
        execute_system_line("$X");
        return;
    }
    if(strncmp(line, "@SPINDLE_ON ", 12) == 0) {
        handle_spindle_on(line);
        return;
    }
    if(strcmp(line, "@SPINDLE_OFF") == 0) {
        enqueue_gcode_line("M5");
        return;
    }
    if(strncmp(line, "@ZERO ", 6) == 0) {
        handle_zero(line);
        return;
    }

    uart_send_line("error:2");
}

void handle_input_line(char* line)
{
    trim_line(line);
    if(is_blank_line(line))
        return;

#if PICO_UART_DEBUG_USB
    Serial.print("[PICO-RX] ");
    Serial.println(line);
#endif

    if(line[0] == '@')
        handle_command(line);
    else
        uart_send_line("error:2");
}

void service_uart_rx()
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
            uart_send_line("error:3");
            continue;
        }

        pico_ctx.rx_line[pico_ctx.rx_length++] = c;
    }
}

void on_state_change(sys_state_t state)
{
    if(pico_ctx.previous_on_state_change != nullptr)
        pico_ctx.previous_on_state_change(state);

    report_state(state);
}

void on_realtime_report(stream_write_ptr stream_write, report_tracking_flags_t report)
{
    if(pico_ctx.previous_on_realtime_report != nullptr)
        pico_ctx.previous_on_realtime_report(stream_write, report);

    maybe_report_position();
}

void pico_framework_service_task(void* data)
{
    (void)data;
    service_uart_rx();
    maybe_report_position();
    task_add_delayed(pico_framework_service_task, NULL, PICO_UART_POLL_MS);
}

void pico_framework_startup_task(void* data)
{
    (void)data;

    pico_uart().begin(PICO_UART_BAUD);
    uart_send_line("@BOOT TEENSY_READY");
    report_state(state_get());
    report_position();
    task_add_delayed(pico_framework_service_task, NULL, PICO_UART_POLL_MS);
}

} // namespace

extern "C" void my_plugin_init(void)
{
    pico_ctx.previous_on_state_change = grbl.on_state_change;
    grbl.on_state_change = on_state_change;

    pico_ctx.previous_on_realtime_report = grbl.on_realtime_report;
    grbl.on_realtime_report = on_realtime_report;

    task_run_on_startup(pico_framework_startup_task, NULL);
}
