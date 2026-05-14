#include "ui/screens/run_screen.h"

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

bool run_screen_field_rect(uint8_t field, ui_rect_t *rect)
{
    if(rect == nullptr)
        return false;

    switch(field) {
        case UiRuntimeField_State:
            *rect = (ui_rect_t){20, 78, 270, 24};
            return true;
        case UiRuntimeField_File:
            *rect = (ui_rect_t){20, 112, 270, 12};
            return true;
        case UiRuntimeField_Time:
            *rect = (ui_rect_t){20, 138, 140, 12};
            return true;
        case UiRuntimeField_Progress:
            *rect = (ui_rect_t){20, 164, 160, 12};
            return true;
        case UiRuntimeField_Position:
            *rect = (ui_rect_t){20, 204, 280, 12};
            return true;
        case UiRuntimeField_Feed:
            *rect = (ui_rect_t){20, 232, 90, 12};
            return true;
        case UiRuntimeField_Spindle:
            *rect = (ui_rect_t){120, 232, 120, 12};
            return true;
        default:
            return false;
    }
}

void run_screen_draw_fields(const ui_runtime_state_t *state, uint8_t fields)
{
    const ui_runtime_state_t *s = screen_state(state);
    ui_rect_t rect = {0};
    char line[64];

    if((fields & UiRuntimeField_State) && run_screen_field_rect(UiRuntimeField_State, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "STATE: %s", s->machine_state);
        tft_display_draw_text(20, 78, line, UI_COLOR_TEXT, UI_COLOR_CARD, 2);
    }

    if((fields & UiRuntimeField_File) && run_screen_field_rect(UiRuntimeField_File, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "FILE: %.32s", s->file_name);
        tft_display_draw_text(20, 112, line, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Time) && run_screen_field_rect(UiRuntimeField_Time, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "TIME: %s", s->elapsed_time);
        tft_display_draw_text(20, 138, line, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Progress) && run_screen_field_rect(UiRuntimeField_Progress, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "PROGRESS: %s", s->progress);
        tft_display_draw_text(20, 164, line, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Position) && run_screen_field_rect(UiRuntimeField_Position, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "X  %s", s->x_position);
        tft_display_draw_text(20, 204, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        snprintf(line, sizeof(line), "Y  %s", s->y_position);
        tft_display_draw_text(120, 204, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        snprintf(line, sizeof(line), "Z  %s", s->z_position);
        tft_display_draw_text(220, 204, line, UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Feed) && run_screen_field_rect(UiRuntimeField_Feed, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "FEED: %s", s->feed);
        tft_display_draw_text(20, 232, line, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    }

    if((fields & UiRuntimeField_Spindle) && run_screen_field_rect(UiRuntimeField_Spindle, &rect)) {
        clear_field(rect);
        snprintf(line, sizeof(line), "SPINDLE: %s", s->spindle);
        tft_display_draw_text(120, 232, line, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    }
}

void run_screen_draw(const ui_runtime_state_t *state)
{
    ui_draw_panel((ui_rect_t){0, UI_CONTENT_TOP, 320, UI_CONTENT_H}, "JOB");
    ui_draw_panel((ui_rect_t){320, UI_CONTENT_TOP, 160, UI_CONTENT_H}, "CONTROL");

    run_screen_draw_fields(state, UiRuntimeField_State |
                                  UiRuntimeField_File |
                                  UiRuntimeField_Time |
                                  UiRuntimeField_Progress |
                                  UiRuntimeField_Position |
                                  UiRuntimeField_Feed |
                                  UiRuntimeField_Spindle);

    ui_draw_button((ui_rect_t){340, 76, 120, 40}, "HOLD", UI_RGB565(176, 116, 38), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){340, 132, 120, 40}, "RESUME", UI_COLOR_SUCCESS, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){340, 188, 120, 40}, "STOP", UI_COLOR_WARNING, UI_COLOR_TEXT, 2);
}
