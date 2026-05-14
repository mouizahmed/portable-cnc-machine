#pragma once

#include <stdint.h>

#define UI_RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define UI_LCD_WIDTH  480
#define UI_LCD_HEIGHT 320
#define UI_TOP_BAR_H 42
#define UI_BOTTOM_BAR_H 46
#define UI_CONTENT_TOP UI_TOP_BAR_H
#define UI_CONTENT_BOTTOM (UI_LCD_HEIGHT - UI_BOTTOM_BAR_H - 1)
#define UI_CONTENT_H (UI_CONTENT_BOTTOM - UI_CONTENT_TOP + 1)
#define UI_PANEL_HEADER_H 18

#define UI_COLOR_BACKGROUND  UI_RGB565(18, 22, 28)
#define UI_COLOR_PANEL       UI_RGB565(24, 30, 38)
#define UI_COLOR_TEXT        UI_RGB565(240, 244, 248)
#define UI_COLOR_MUTED       UI_RGB565(160, 170, 180)
#define UI_COLOR_ACCENT      UI_RGB565(32, 168, 120)
#define UI_COLOR_WARNING     UI_RGB565(192, 72, 42)
#define UI_COLOR_BUTTON      UI_RGB565(46, 92, 168)
#define UI_COLOR_BORDER      UI_RGB565(88, 100, 112)
#define UI_COLOR_CARD        UI_RGB565(30, 40, 50)
#define UI_COLOR_CARD_ACCENT UI_RGB565(38, 52, 66)
#define UI_COLOR_CARD_BODY   UI_RGB565(26, 35, 44)
#define UI_COLOR_BUTTON_ALT  UI_RGB565(92, 102, 118)
#define UI_COLOR_SUCCESS     UI_RGB565(48, 176, 96)
#define UI_COLOR_TARGET      UI_RGB565(68, 136, 212)
#define UI_COLOR_TRACE       UI_RGB565(255, 210, 0)

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} ui_rect_t;

typedef enum {
    UiScreen_Home = 0,
    UiScreen_Jog,
    UiScreen_Files,
    UiScreen_Run,
    UiScreen_Status,
    UiScreen_Settings
} ui_screen_t;
