#include "ui/screens/settings_screen.h"

#include "ui/components/ui_components.h"
#include "ui/display/tft_display.h"
#include "ui/ui_types.h"

void settings_screen_draw(void)
{
    ui_draw_panel((ui_rect_t){0, UI_CONTENT_TOP, 282, UI_CONTENT_H}, "SETTINGS");
    ui_draw_panel((ui_rect_t){282, UI_CONTENT_TOP, 198, UI_CONTENT_H}, "EDITOR");
    tft_display_draw_text(20, 74, "PAGE 1/4  PROFILE", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    ui_draw_button((ui_rect_t){20, 102, 242, 24}, "Steps/mm X              --", UI_COLOR_ACCENT, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){20, 130, 242, 24}, "Steps/mm Y              --", UI_COLOR_CARD, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){20, 158, 242, 24}, "Steps/mm Z              --", UI_COLOR_CARD, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){20, 186, 242, 24}, "Travel X                --", UI_COLOR_CARD, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){20, 214, 242, 24}, "Travel Y                --", UI_COLOR_CARD, UI_COLOR_TEXT, 1);
    ui_draw_centered_text((ui_rect_t){294, 76, 174, 16}, "Steps/mm X", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    ui_draw_centered_text((ui_rect_t){294, 106, 174, 24}, "--", UI_COLOR_TARGET, UI_COLOR_CARD, 2);
    ui_draw_button((ui_rect_t){306, 154, 64, 28}, "-", UI_COLOR_BUTTON_ALT, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){386, 154, 64, 28}, "+", UI_COLOR_BUTTON_ALT, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){306, 204, 64, 24}, "SAVE", UI_COLOR_BUTTON, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){386, 204, 64, 24}, "REVERT", UI_COLOR_WARNING, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){306, 238, 144, 24}, "DEFAULTS", UI_RGB565(156, 96, 34), UI_COLOR_TEXT, 1);
}
