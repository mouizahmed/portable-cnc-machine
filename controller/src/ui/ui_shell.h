#pragma once

#include <stdint.h>

#include "storage/sd_gcode_files.h"
#include "ui/ui_runtime_state.h"
#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_shell_show_calibration_target(uint8_t index, uint16_t x, uint16_t y);
void ui_shell_show_calibration_done(void);
void ui_shell_show(ui_screen_t active_screen, const sd_gcode_file_list_t *files, int8_t selected_file_index);
void ui_shell_set_runtime_state(const ui_runtime_state_t *state);
void ui_shell_update_runtime_fields(ui_screen_t active_screen, uint8_t fields);
void ui_shell_show_screen_change(ui_screen_t previous_screen,
                                 ui_screen_t active_screen,
                                 const sd_gcode_file_list_t *files,
                                 int8_t selected_file_index);
void ui_shell_update_file_selection(const sd_gcode_file_list_t *files,
                                    int8_t previous_index,
                                    int8_t selected_index);
void ui_shell_update_touch(ui_screen_t active_screen,
                           const sd_gcode_file_list_t *files,
                           uint16_t x,
                           uint16_t y,
                           const char *label);

#ifdef __cplusplus
}
#endif
