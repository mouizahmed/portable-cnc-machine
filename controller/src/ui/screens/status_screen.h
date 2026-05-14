#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_runtime_state.h"
#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void status_screen_draw(const ui_runtime_state_t *state);
void status_screen_draw_fields(const ui_runtime_state_t *state, uint8_t fields);
bool status_screen_field_rect(uint8_t field, ui_rect_t *rect);

#ifdef __cplusplus
}
#endif
