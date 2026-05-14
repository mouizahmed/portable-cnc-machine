#include "ui/screens/status_screen.h"

#include "ui/components/ui_components.h"
#include "ui/display/tft_display.h"
#include "ui/ui_types.h"

#include <stdio.h>

static const ui_runtime_state_t fallback_state = {
    "IDLE", "--", "00:00", "--", "0.000", "0.000", "0.000", "--", "--", "ENABLED", "ENABLED", "CALIBRATED"
};

static const ui_runtime_state_t *screen_state(const ui_runtime_state_t *state)
{
    return state == nullptr ? &fallback_state : state;
}

static void clear_field(ui_rect_t rect)
{
    tft_display_fill_rect(rect, UI_COLOR_CARD);
}

bool status_screen_field_rect(uint8_t field, ui_rect_t *rect)
{
    if(rect == nullptr)
        return false;

    switch(field) {
        case UiRuntimeField_State:
            *rect = (ui_rect_t){20, 102, 180, 12};
            return true;
        case UiRuntimeField_Position:
            *rect = (ui_rect_t){20, 126, 190, 12};
            return true;
        case UiRuntimeField_Storage:
            *rect = (ui_rect_t){260, 78, 190, 60};
            return true;
        default:
            return false;
    }
}

void status_screen_draw_fields(const ui_runtime_state_t *state, uint8_t fields)
{
    const ui_runtime_state_t *s = screen_state(state);
    ui_rect_t rect = {0};
    char line[48];

    if((fields & UiRuntimeField_State) && status_screen_field_rect(UiRuntimeField_State, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "STATE: %s", s->machine_state);
        tft_display_draw_text(20, 102, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Position) && status_screen_field_rect(UiRuntimeField_Position, &rect)) {
        clear_field(rect);
        tft_display_draw_text(20, 126, "MOTION: NOT WIRED", UI_COLOR_WARNING, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Storage) && status_screen_field_rect(UiRuntimeField_Storage, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "SD: %s", s->sd_status);
        tft_display_draw_text(260, 78, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        snprintf(line, sizeof(line), "LittleFS: %s", s->littlefs_status);
        tft_display_draw_text(260, 102, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        snprintf(line, sizeof(line), "TOUCH: %s", s->touch_status);
        tft_display_draw_text(260, 126, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    }
}

void status_screen_draw(const ui_runtime_state_t *state)
{
    ui_draw_panel((ui_rect_t){0, UI_CONTENT_TOP, 240, UI_CONTENT_H}, "CONTROLLER");
    ui_draw_panel((ui_rect_t){240, UI_CONTENT_TOP, 240, UI_CONTENT_H}, "STORAGE");
    tft_display_draw_text(20, 78, "FIRMWARE: grblHAL", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    status_screen_draw_fields(state, UiRuntimeField_State | UiRuntimeField_Position | UiRuntimeField_Storage);
}
