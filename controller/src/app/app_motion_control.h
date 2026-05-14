#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AppMotionResult_Ok = 0,
    AppMotionResult_InvalidAxis,
    AppMotionResult_InvalidDistance,
    AppMotionResult_InvalidFeed,
    AppMotionResult_NotIdle,
    AppMotionResult_HomingRequired,
    AppMotionResult_Limit,
    AppMotionResult_Failed
} app_motion_result_t;

app_motion_result_t app_motion_jog(uint8_t axis, float distance_mm, uint16_t feed_mm_min);
app_motion_result_t app_motion_jog_cancel(void);
const char *app_motion_result_reason(app_motion_result_t result);

#ifdef __cplusplus
}
#endif
