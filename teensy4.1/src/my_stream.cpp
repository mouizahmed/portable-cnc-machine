#include <Arduino.h>
#include <string.h>
#include <ctype.h>
#include "grbl/hal.h"
#include "my_stream.h"

static bool use_uart = false;

static char cmd_buffer[64];
static uint8_t cmd_index = 0;

// ===== CLEAN STRING FUNCTION =====
void clean_command(char *cmd)
{
    // Remove CR/LF
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '\r' || cmd[i] == '\n')
            cmd[i] = '\0';
    }

    // Trim trailing spaces
    int len = strlen(cmd);
    while (len > 0 && isspace(cmd[len - 1])) {
        cmd[len - 1] = '\0';
        len--;
    }
}

// ===== STREAM READ =====
int32_t my_stream_read(void)
{
    int c = -1;

    if (use_uart) {
        if (Serial1.available())
            c = Serial1.read();
    } else {
        if (Serial.available())
            c = Serial.read();
    }

    if (c == -1)
        return -1;

    // Ignore carriage return
    if (c == '\r')
        return -1;

    // End of line detected
    if (c == '\n') {

        cmd_buffer[cmd_index] = '\0';

        clean_command(cmd_buffer);

        // DEBUG print
        Serial.print("\n[CMD CLEAN: ");
        Serial.print(cmd_buffer);
        Serial.println("]");

        // ===== SWITCH COMMANDS =====
        if (strcasecmp(cmd_buffer, "UART") == 0) {
            use_uart = true;
            Serial.println("[Switched to UART]");
            cmd_index = 0;
            return -1; // do NOT send to GRBL
        }

        if (strcasecmp(cmd_buffer, "USB") == 0) {
            use_uart = false;
            Serial.println("[Switched to USB]");
            cmd_index = 0;
            return -1;
        }

        cmd_index = 0;
    }
    else {
        if (cmd_index < sizeof(cmd_buffer) - 1) {
            cmd_buffer[cmd_index++] = (char)c;
        }
    }

    return c; // send to GRBL
}

// ===== STREAM WRITE =====
void my_stream_write(const char *s)
{
    while (*s)
        Serial.write(*s++);
}

// ===== INIT =====
void stream_init(void)
{
    hal.stream.read = my_stream_read;
    hal.stream.write = my_stream_write;
    Serial.println("Stream override active!");
}