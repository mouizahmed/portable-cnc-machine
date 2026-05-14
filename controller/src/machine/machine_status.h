#pragma once

#include "app/app_machine_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    app_machine_mode_t mode;
    float mpos_x;
    float mpos_y;
    float mpos_z;
    float feed_rate;
    float spindle_rpm;
} machine_status_snapshot_t;

void machine_status_snapshot(machine_status_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
