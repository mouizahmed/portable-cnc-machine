#pragma once

#include <stdint.h>

#include "ui/ui_runtime_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_ui_state_init(void);
const ui_runtime_state_t *app_ui_state_snapshot(void);
uint8_t app_ui_state_refresh(void);

#ifdef __cplusplus
}
#endif
