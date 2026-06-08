#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UiRuntimeField_State    = 1u << 0,
    UiRuntimeField_File     = 1u << 1,
    UiRuntimeField_Time     = 1u << 2,
    UiRuntimeField_Progress = 1u << 3,
    UiRuntimeField_Position = 1u << 4,
    UiRuntimeField_Feed     = 1u << 5,
    UiRuntimeField_Spindle  = 1u << 6,
    UiRuntimeField_Storage  = 1u << 7,
    UiRuntimeField_All      = 0xFFu
} ui_runtime_field_mask_t;

typedef struct {
    char machine_state[12];
    char file_name[40];
    char elapsed_time[12];
    char progress[16];
    char x_position[16];
    char y_position[16];
    char z_position[16];
    char feed[16];
    char spindle[16];
    char sd_status[16];
    char littlefs_status[16];
    char touch_status[16];
    char temperature[16];
} ui_runtime_state_t;

#ifdef __cplusplus
}
#endif
