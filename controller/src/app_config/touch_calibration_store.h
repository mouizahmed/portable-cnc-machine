#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t screen_x;
    uint16_t screen_y;
    uint16_t raw_x;
    uint16_t raw_y;
} touch_calibration_point_t;

typedef struct {
    touch_calibration_point_t points[4];
} touch_calibration_t;

bool touch_calibration_load(touch_calibration_t *calibration);
bool touch_calibration_save(const touch_calibration_t *calibration);
bool touch_calibration_map_raw(const touch_calibration_t *calibration,
                               uint16_t raw_x,
                               uint16_t raw_y,
                               uint16_t *screen_x,
                               uint16_t *screen_y);
