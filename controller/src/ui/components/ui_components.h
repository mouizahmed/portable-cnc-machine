#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_draw_centered_text(ui_rect_t rect, const char *text, uint16_t fg, uint16_t bg, uint8_t scale);
void ui_draw_button(ui_rect_t rect, const char *label, uint16_t fill, uint16_t text_color, uint8_t scale);
void ui_draw_panel(ui_rect_t rect, const char *title);
void ui_draw_menu_card(ui_rect_t rect, const char *title, const char *subtitle, bool enabled);

#ifdef __cplusplus
}
#endif
