#pragma once

#include <stdint.h>

#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

ui_screen_t app_ui_controller_active_screen(void);
void app_ui_controller_show_current(void);
void app_ui_controller_show_calibration_target(uint8_t index, uint16_t x, uint16_t y);
void app_ui_controller_handle_touch(uint16_t x, uint16_t y);
void app_ui_controller_update_runtime_fields(uint8_t fields);

#ifdef __cplusplus
}
#endif
