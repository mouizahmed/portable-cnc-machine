#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SD_GCODE_FILE_MAX_COUNT 32
#define SD_GCODE_FILE_NAME_MAX  64

typedef struct {
    char names[SD_GCODE_FILE_MAX_COUNT][SD_GCODE_FILE_NAME_MAX];
    uint32_t sizes[SD_GCODE_FILE_MAX_COUNT];
    char first_seen[3][SD_GCODE_FILE_NAME_MAX];
    uint8_t count;
    uint8_t seen_count;
    bool sd_ready;
    bool truncated;
} sd_gcode_file_list_t;

bool sd_gcode_files_refresh(sd_gcode_file_list_t *list);
