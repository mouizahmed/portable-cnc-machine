#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_dirty_draw_rect_fn)(ui_rect_t rect, void *context);

void ui_dirty_reset(void);
bool ui_dirty_mark(ui_rect_t rect);
bool ui_dirty_mark_full(void);
bool ui_dirty_has_pending(void);
uint8_t ui_dirty_count(void);
void ui_dirty_flush(ui_dirty_draw_rect_fn draw_rect, void *context);

#ifdef __cplusplus
}
#endif
