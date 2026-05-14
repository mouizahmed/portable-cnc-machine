#include "app/app_machine_state.h"

static app_machine_mode_t current_mode = AppMachineMode_Idle;

app_machine_mode_t app_machine_state_mode(void)
{
    return current_mode;
}

void app_machine_state_set_mode(app_machine_mode_t mode)
{
    current_mode = mode;
}

const char *app_machine_state_label(app_machine_mode_t mode)
{
    switch(mode) {
        case AppMachineMode_Jogging:
            return "JOG";
        case AppMachineMode_RunningJob:
            return "RUN";
        case AppMachineMode_Holding:
            return "HOLD";
        case AppMachineMode_Alarm:
            return "ALARM";
        case AppMachineMode_Idle:
        default:
            return "IDLE";
    }
}

bool app_machine_state_screen_allowed(ui_screen_t screen)
{
    switch(current_mode) {
        case AppMachineMode_RunningJob:
        case AppMachineMode_Holding:
            return screen == UiScreen_Run || screen == UiScreen_Status;

        case AppMachineMode_Jogging:
            return screen == UiScreen_Home || screen == UiScreen_Jog || screen == UiScreen_Status;

        case AppMachineMode_Alarm:
            return screen == UiScreen_Run || screen == UiScreen_Status || screen == UiScreen_Settings;

        case AppMachineMode_Idle:
        default:
            return true;
    }
}

ui_screen_t app_machine_state_locked_screen(void)
{
    switch(current_mode) {
        case AppMachineMode_RunningJob:
        case AppMachineMode_Holding:
        case AppMachineMode_Alarm:
            return UiScreen_Run;

        case AppMachineMode_Jogging:
            return UiScreen_Jog;

        case AppMachineMode_Idle:
        default:
            return UiScreen_Home;
    }
}
