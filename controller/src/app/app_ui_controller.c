#include "app/app_ui_controller.h"

#include "app/app_file_service.h"
#include "app/app_machine_state.h"
#include "app/app_motion_control.h"
#include "protocol/protocol_defs.h"
#include "ui/screens/files_screen.h"
#include "ui/screens/jog_screen.h"
#include "ui/ui_shell.h"

#include <stddef.h>

typedef struct {
    const char *label;
    ui_screen_t screen;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} ui_nav_button_t;

static const ui_nav_button_t ui_nav_buttons[] = {
    { .label = "HOME",  .screen = UiScreen_Home,     .x = 0,   .y = 274, .w = 80, .h = 46 },
    { .label = "JOG",   .screen = UiScreen_Jog,      .x = 80,  .y = 274, .w = 80, .h = 46 },
    { .label = "FILES", .screen = UiScreen_Files,    .x = 160, .y = 274, .w = 80, .h = 46 },
    { .label = "RUN",   .screen = UiScreen_Run,      .x = 240, .y = 274, .w = 80, .h = 46 },
    { .label = "STAT",  .screen = UiScreen_Status,   .x = 320, .y = 274, .w = 80, .h = 46 },
    { .label = "SET",   .screen = UiScreen_Settings, .x = 400, .y = 274, .w = 80, .h = 46 }
};

static ui_screen_t active_screen = UiScreen_Home;

static bool handle_jog_touch(uint16_t x, uint16_t y)
{
    jog_screen_action_t action = JogScreenAction_None;
    if(!jog_screen_hit_test(x, y, &action))
        return false;

    switch(action) {
        case JogScreenAction_XMinus:
            app_motion_jog(AXIS_X, -jog_screen_step_mm(), jog_screen_feed_mm_min());
            break;
        case JogScreenAction_XPlus:
            app_motion_jog(AXIS_X, jog_screen_step_mm(), jog_screen_feed_mm_min());
            break;
        case JogScreenAction_YMinus:
            app_motion_jog(AXIS_Y, -jog_screen_step_mm(), jog_screen_feed_mm_min());
            break;
        case JogScreenAction_YPlus:
            app_motion_jog(AXIS_Y, jog_screen_step_mm(), jog_screen_feed_mm_min());
            break;
        case JogScreenAction_ZMinus:
            app_motion_jog(AXIS_Z, -jog_screen_step_mm(), jog_screen_feed_mm_min());
            break;
        case JogScreenAction_ZPlus:
            app_motion_jog(AXIS_Z, jog_screen_step_mm(), jog_screen_feed_mm_min());
            break;
        case JogScreenAction_Stop:
            app_motion_jog_cancel();
            break;
        case JogScreenAction_Step01:
        case JogScreenAction_Step1:
        case JogScreenAction_Step10:
        case JogScreenAction_FeedSlow:
        case JogScreenAction_FeedMedium:
        case JogScreenAction_FeedFast:
            jog_screen_apply_selection(action);
#if CNC_ENABLE_TFT
            ui_shell_show(active_screen, app_file_service_files(), app_file_service_selected_index());
#endif
            break;
        case JogScreenAction_None:
        default:
            break;
    }

    return true;
}

static const char *ui_hit_label(uint16_t x, uint16_t y, ui_screen_t *screen)
{
    for(uint8_t i = 0; i < sizeof(ui_nav_buttons) / sizeof(ui_nav_buttons[0]); i++) {
        const ui_nav_button_t *button = &ui_nav_buttons[i];

        if(x >= button->x && x < button->x + button->w &&
           y >= button->y && y < button->y + button->h) {
            if(screen != NULL)
                *screen = button->screen;
            return button->label;
        }
    }

    return "";
}

ui_screen_t app_ui_controller_active_screen(void)
{
    return active_screen;
}

void app_ui_controller_show_current(void)
{
#if CNC_ENABLE_TFT
    ui_shell_show(active_screen, app_file_service_files(), app_file_service_selected_index());
#endif
}

void app_ui_controller_show_calibration_target(uint8_t index, uint16_t x, uint16_t y)
{
#if CNC_ENABLE_TFT
    ui_shell_show_calibration_target(index, x, y);
#else
    (void)index;
    (void)x;
    (void)y;
#endif
}

void app_ui_controller_handle_touch(uint16_t x, uint16_t y)
{
    ui_screen_t hit_screen = active_screen;
    const char *label = ui_hit_label(x, y, &hit_screen);

    if(label[0] == '\0') {
        if(active_screen == UiScreen_Jog) {
            handle_jog_touch(x, y);
            return;
        }

        if(active_screen == UiScreen_Files && app_machine_state_mode() == AppMachineMode_Idle) {
            uint8_t file_index = 0;
            int8_t previous_index = -1;
            const sd_gcode_file_list_t *files = app_file_service_files();

            if(files_screen_hit_test_row(x, y, files, &file_index) &&
               app_file_service_select_index(file_index, &previous_index)) {
#if CNC_ENABLE_TFT
                ui_shell_update_file_selection(files, previous_index, app_file_service_selected_index());
#endif
            }
        }
        return;
    }

    if(!app_machine_state_screen_allowed(hit_screen))
        hit_screen = app_machine_state_locked_screen();

    if(hit_screen == active_screen)
        return;

    const ui_screen_t previous_screen = active_screen;
    active_screen = hit_screen;

    if(active_screen == UiScreen_Files && app_machine_state_mode() == AppMachineMode_Idle)
        app_file_service_refresh(true);

#if CNC_ENABLE_TFT
    ui_shell_show_screen_change(previous_screen,
                                active_screen,
                                app_file_service_files(),
                                app_file_service_selected_index());
#endif
}

void app_ui_controller_update_runtime_fields(uint8_t fields)
{
#if CNC_ENABLE_TFT
    if(fields != 0)
        ui_shell_update_runtime_fields(active_screen, fields);
#else
    (void)fields;
#endif
}
