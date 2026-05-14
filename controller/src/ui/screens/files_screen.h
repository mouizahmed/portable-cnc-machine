#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "storage/sd_gcode_files.h"
#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void files_screen_draw(const sd_gcode_file_list_t *files, int8_t selected_index);
void files_screen_draw_row(const sd_gcode_file_list_t *files, uint8_t index, int8_t selected_index);
void files_screen_draw_details(const sd_gcode_file_list_t *files, int8_t selected_index);
bool files_screen_hit_test_row(uint16_t x, uint16_t y, const sd_gcode_file_list_t *files, uint8_t *index);
bool files_screen_row_rect(uint8_t index, ui_rect_t *rect);
ui_rect_t files_screen_details_rect(void);

#ifdef __cplusplus
}
#endif
