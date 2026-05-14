#include "app_config/touch_calibration_store.h"

#include <stdio.h>
#include <string.h>

#include "grbl/vfs.h"

#define TOUCH_CALIBRATION_FILE "/littlefs/app/touch_calibration.json"

static uint16_t clamp_u16(int32_t value, uint16_t min_value, uint16_t max_value)
{
    if(value < (int32_t)min_value)
        return min_value;

    if(value > (int32_t)max_value)
        return max_value;

    return (uint16_t)value;
}

static uint16_t map_axis(int32_t raw,
                         int32_t raw_a,
                         int32_t raw_b,
                         uint16_t screen_a,
                         uint16_t screen_b)
{
    const int32_t denom = raw_b - raw_a;
    if(denom == 0)
        return screen_a;

    const int32_t mapped = (int32_t)screen_a +
                           ((raw - raw_a) * ((int32_t)screen_b - (int32_t)screen_a)) / denom;

    return screen_a < screen_b
               ? clamp_u16(mapped, screen_a, screen_b)
               : clamp_u16(mapped, screen_b, screen_a);
}

bool touch_calibration_load(touch_calibration_t *calibration)
{
    if(calibration == NULL)
        return false;

    vfs_file_t *file = vfs_open(TOUCH_CALIBRATION_FILE, "r");
    if(file == NULL)
        return false;

    char buffer[384] = {0};
    const size_t count = vfs_read(buffer, 1, sizeof(buffer) - 1, file);
    vfs_close(file);

    if(count == 0 || strstr(buffer, "\"schema\":1") == NULL)
        return false;

    int values[16] = {0};
    if(sscanf(buffer,
              "{\"schema\":1,\"points\":[{\"sx\":%d,\"sy\":%d,\"rx\":%d,\"ry\":%d},{\"sx\":%d,\"sy\":%d,\"rx\":%d,\"ry\":%d},{\"sx\":%d,\"sy\":%d,\"rx\":%d,\"ry\":%d},{\"sx\":%d,\"sy\":%d,\"rx\":%d,\"ry\":%d}]}",
              &values[0], &values[1], &values[2], &values[3],
              &values[4], &values[5], &values[6], &values[7],
              &values[8], &values[9], &values[10], &values[11],
              &values[12], &values[13], &values[14], &values[15]) != 16)
        return false;

    for(uint8_t i = 0; i < 4; i++) {
        calibration->points[i].screen_x = (uint16_t)values[i * 4 + 0];
        calibration->points[i].screen_y = (uint16_t)values[i * 4 + 1];
        calibration->points[i].raw_x = (uint16_t)values[i * 4 + 2];
        calibration->points[i].raw_y = (uint16_t)values[i * 4 + 3];
    }

    return true;
}

bool touch_calibration_save(const touch_calibration_t *calibration)
{
    if(calibration == NULL)
        return false;

    char buffer[384];
    const int len = snprintf(buffer, sizeof(buffer),
                             "{\"schema\":1,\"points\":["
                             "{\"sx\":%u,\"sy\":%u,\"rx\":%u,\"ry\":%u},"
                             "{\"sx\":%u,\"sy\":%u,\"rx\":%u,\"ry\":%u},"
                             "{\"sx\":%u,\"sy\":%u,\"rx\":%u,\"ry\":%u},"
                             "{\"sx\":%u,\"sy\":%u,\"rx\":%u,\"ry\":%u}"
                             "]}\n",
                             calibration->points[0].screen_x, calibration->points[0].screen_y, calibration->points[0].raw_x, calibration->points[0].raw_y,
                             calibration->points[1].screen_x, calibration->points[1].screen_y, calibration->points[1].raw_x, calibration->points[1].raw_y,
                             calibration->points[2].screen_x, calibration->points[2].screen_y, calibration->points[2].raw_x, calibration->points[2].raw_y,
                             calibration->points[3].screen_x, calibration->points[3].screen_y, calibration->points[3].raw_x, calibration->points[3].raw_y);

    if(len <= 0 || len >= (int)sizeof(buffer))
        return false;

    vfs_file_t *file = vfs_open(TOUCH_CALIBRATION_FILE, "w");
    if(file == NULL)
        return false;

    const size_t written = vfs_write(buffer, 1, (size_t)len, file);
    vfs_close(file);

    return written == (size_t)len;
}

bool touch_calibration_map_raw(const touch_calibration_t *calibration,
                               uint16_t raw_x,
                               uint16_t raw_y,
                               uint16_t *screen_x,
                               uint16_t *screen_y)
{
    if(calibration == NULL || screen_x == NULL || screen_y == NULL)
        return false;

    const touch_calibration_point_t *tl = &calibration->points[0];
    const touch_calibration_point_t *tr = &calibration->points[1];
    const touch_calibration_point_t *br = &calibration->points[2];
    const touch_calibration_point_t *bl = &calibration->points[3];

    const int32_t raw_left_y = ((int32_t)tl->raw_y + (int32_t)bl->raw_y) / 2;
    const int32_t raw_right_y = ((int32_t)tr->raw_y + (int32_t)br->raw_y) / 2;
    const int32_t raw_top_x = ((int32_t)tl->raw_x + (int32_t)tr->raw_x) / 2;
    const int32_t raw_bottom_x = ((int32_t)bl->raw_x + (int32_t)br->raw_x) / 2;

    const uint16_t screen_left_x = (uint16_t)(((uint32_t)tl->screen_x + (uint32_t)bl->screen_x) / 2);
    const uint16_t screen_right_x = (uint16_t)(((uint32_t)tr->screen_x + (uint32_t)br->screen_x) / 2);
    const uint16_t screen_top_y = (uint16_t)(((uint32_t)tl->screen_y + (uint32_t)tr->screen_y) / 2);
    const uint16_t screen_bottom_y = (uint16_t)(((uint32_t)bl->screen_y + (uint32_t)br->screen_y) / 2);

    *screen_x = map_axis(raw_y, raw_left_y, raw_right_y, screen_left_x, screen_right_x);
    *screen_y = map_axis(raw_x, raw_top_x, raw_bottom_x, screen_top_y, screen_bottom_y);

    return true;
}
