#include "app/app_ui_state.h"

#include "app/app_machine_state.h"
#include "app/app_bmp280_sensor.h"
#include "machine/machine_status.h"

#include <stdio.h>
#include <string.h>

#include "grbl/nuts_bolts.h"

static ui_runtime_state_t current_state;
static uint32_t refresh_ticks = 0;

static uint8_t copy_field(char *dest, size_t dest_size, const char *source, uint8_t mask)
{
    if(strncmp(dest, source, dest_size) == 0)
        return 0;

    snprintf(dest, dest_size, "%.*s", (int)dest_size - 1, source);
    return mask;
}

static uint8_t copy_position(char *dest, size_t dest_size, float value, uint8_t mask)
{
    return copy_field(dest, dest_size, ftoa(value, 3), mask);
}

void app_ui_state_init(void)
{
    memset(&current_state, 0, sizeof(current_state));

    snprintf(current_state.machine_state, sizeof(current_state.machine_state), "IDLE");
    snprintf(current_state.file_name, sizeof(current_state.file_name), "--");
    snprintf(current_state.elapsed_time, sizeof(current_state.elapsed_time), "00:00");
    snprintf(current_state.progress, sizeof(current_state.progress), "--");
    snprintf(current_state.x_position, sizeof(current_state.x_position), "0.000");
    snprintf(current_state.y_position, sizeof(current_state.y_position), "0.000");
    snprintf(current_state.z_position, sizeof(current_state.z_position), "0.000");
    snprintf(current_state.feed, sizeof(current_state.feed), "--");
    snprintf(current_state.spindle, sizeof(current_state.spindle), "--");
    snprintf(current_state.sd_status, sizeof(current_state.sd_status), "ENABLED");
    snprintf(current_state.littlefs_status, sizeof(current_state.littlefs_status), "ENABLED");
    snprintf(current_state.touch_status, sizeof(current_state.touch_status), "CALIBRATED");
    snprintf(current_state.temperature, sizeof(current_state.temperature), "--");
}

const ui_runtime_state_t *app_ui_state_snapshot(void)
{
    return &current_state;
}

uint8_t app_ui_state_refresh(void)
{
    uint8_t dirty = 0;
    char buffer[16];
    machine_status_snapshot_t machine = {0};

    machine_status_snapshot(&machine);
    app_machine_state_set_mode(machine.mode);

    const app_machine_mode_t mode = machine.mode;

    dirty |= copy_field(current_state.machine_state, sizeof(current_state.machine_state),
                        app_machine_state_label(mode), UiRuntimeField_State);

    dirty |= copy_position(current_state.x_position, sizeof(current_state.x_position),
                           machine.mpos_x, UiRuntimeField_Position);
    dirty |= copy_position(current_state.y_position, sizeof(current_state.y_position),
                           machine.mpos_y, UiRuntimeField_Position);
    dirty |= copy_position(current_state.z_position, sizeof(current_state.z_position),
                           machine.mpos_z, UiRuntimeField_Position);

    if(mode == AppMachineMode_RunningJob || mode == AppMachineMode_Holding) {
        refresh_ticks++;
        snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
                 (unsigned long)(refresh_ticks / 120u),
                 (unsigned long)((refresh_ticks / 2u) % 60u));
        dirty |= copy_field(current_state.elapsed_time, sizeof(current_state.elapsed_time),
                            buffer, UiRuntimeField_Time);
    }

    float temperature_c = 0.0f;
    if(app_bmp280_sensor_temperature_c(&temperature_c))
        snprintf(buffer, sizeof(buffer), "%.1f C", temperature_c);
    else
        snprintf(buffer, sizeof(buffer), "--");

    dirty |= copy_field(current_state.temperature, sizeof(current_state.temperature),
                        buffer, UiRuntimeField_Storage);

    return dirty;
}
