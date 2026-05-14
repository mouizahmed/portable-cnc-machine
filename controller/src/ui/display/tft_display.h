#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool tft_display_init(void);
void tft_display_fill_screen(uint16_t color);
void tft_display_fill_rect(ui_rect_t rect, uint16_t color);
void tft_display_draw_rect(ui_rect_t rect, uint16_t color);
void tft_display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void tft_display_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void tft_display_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color);
void tft_display_draw_text(int16_t x, int16_t y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale);
int16_t tft_display_text_width(const char *text, uint8_t scale);
int16_t tft_display_text_height(uint8_t scale);

#ifdef __cplusplus
}
#endif
