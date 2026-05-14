#include "ui/components/ui_components.h"

#include <stddef.h>

#include "ui/display/tft_display.h"

static int16_t centered_x(ui_rect_t rect, const char *text, uint8_t scale)
{
    return (int16_t)(rect.x + (rect.w - tft_display_text_width(text, scale)) / 2);
}

static int16_t centered_y(ui_rect_t rect, uint8_t scale)
{
    return (int16_t)(rect.y + (rect.h - tft_display_text_height(scale)) / 2);
}

void ui_draw_centered_text(ui_rect_t rect, const char *text, uint16_t fg, uint16_t bg, uint8_t scale)
{
    tft_display_draw_text(centered_x(rect, text, scale), centered_y(rect, scale), text, fg, bg, scale);
}

void ui_draw_button(ui_rect_t rect, const char *label, uint16_t fill, uint16_t text_color, uint8_t scale)
{
    tft_display_fill_rect(rect, fill);
    tft_display_draw_rect(rect, UI_COLOR_BORDER);
    ui_draw_centered_text(rect, label, text_color, fill, scale);
}

void ui_draw_panel(ui_rect_t rect, const char *title)
{
    tft_display_fill_rect(rect, UI_COLOR_CARD);
    tft_display_draw_rect(rect, UI_COLOR_BORDER);
    tft_display_fill_rect((ui_rect_t){
                              (int16_t)(rect.x + 1),
                              (int16_t)(rect.y + 1),
                              (int16_t)(rect.w - 2),
                              UI_PANEL_HEADER_H
                          },
                          UI_COLOR_CARD_ACCENT);

    if(title != NULL)
        ui_draw_centered_text((ui_rect_t){rect.x, (int16_t)(rect.y + 1), rect.w, UI_PANEL_HEADER_H},
                              title, UI_COLOR_TEXT, UI_COLOR_CARD_ACCENT, 1);
}

void ui_draw_menu_card(ui_rect_t rect, const char *title, const char *subtitle, bool enabled)
{
    const uint16_t fill = enabled ? UI_COLOR_CARD : UI_RGB565(34, 38, 44);
    const uint16_t title_color = enabled ? UI_COLOR_TEXT : UI_COLOR_MUTED;

    tft_display_fill_rect(rect, fill);
    tft_display_draw_rect(rect, UI_COLOR_BORDER);
    tft_display_fill_rect((ui_rect_t){
                              (int16_t)(rect.x + 1),
                              (int16_t)(rect.y + 1),
                              (int16_t)(rect.w - 2),
                              14
                          },
                          UI_COLOR_CARD_ACCENT);
    ui_draw_centered_text((ui_rect_t){rect.x, (int16_t)(rect.y + 42), rect.w, 22}, title, title_color, fill, 2);
    ui_draw_centered_text((ui_rect_t){rect.x, (int16_t)(rect.y + 76), rect.w, 12}, subtitle, UI_COLOR_MUTED, fill, 1);
}
