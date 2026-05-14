#pragma once

#include <stdbool.h>

#include "storage/sd_gcode_files.h"

#ifdef __cplusplus
extern "C" {
#endif

const sd_gcode_file_list_t *app_file_service_files(void);
void app_file_service_refresh(bool report);
int8_t app_file_service_selected_index(void);
bool app_file_service_select_index(uint8_t index, int8_t *previous_index);

#ifdef __cplusplus
}
#endif
