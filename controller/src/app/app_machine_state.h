#pragma once

#include <stdbool.h>

#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AppMachineMode_Idle = 0,
    AppMachineMode_Jogging,
    AppMachineMode_RunningJob,
    AppMachineMode_Holding,
    AppMachineMode_Alarm
} app_machine_mode_t;

app_machine_mode_t app_machine_state_mode(void);
void app_machine_state_set_mode(app_machine_mode_t mode);
const char *app_machine_state_label(app_machine_mode_t mode);
bool app_machine_state_screen_allowed(ui_screen_t screen);
ui_screen_t app_machine_state_locked_screen(void);

#ifdef __cplusplus
}
#endif
