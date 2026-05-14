#include "ui/screens/jog_screen.h"

#include "ui/components/ui_components.h"
#include "ui/display/tft_display.h"
#include "ui/ui_types.h"

#include <stdio.h>

typedef struct {
    jog_screen_action_t action;
    ui_rect_t rect;
} jog_button_hit_t;

static const jog_button_hit_t jog_buttons[] = {
    { JogScreenAction_YPlus,      {222, 84,  36, 36} },
    { JogScreenAction_YMinus,     {222, 166, 36, 36} },
    { JogScreenAction_XMinus,     {180, 125, 36, 36} },
    { JogScreenAction_XPlus,      {264, 125, 36, 36} },
    { JogScreenAction_Stop,       {222, 125, 36, 36} },
    { JogScreenAction_ZPlus,      {178, 219, 56, 30} },
    { JogScreenAction_ZMinus,     {246, 219, 56, 30} },
    { JogScreenAction_Step01,     {335, 94,  40, 32} },
    { JogScreenAction_Step1,      {380, 94,  40, 32} },
    { JogScreenAction_Step10,     {425, 94,  40, 32} },
    { JogScreenAction_FeedSlow,   {335, 209, 40, 32} },
    { JogScreenAction_FeedMedium, {380, 209, 40, 32} },
    { JogScreenAction_FeedFast,   {425, 209, 40, 32} }
};

static float selected_step_mm = 1.0f;
static uint16_t selected_feed_mm_min = 500u;

static bool rect_contains(ui_rect_t rect, uint16_t x, uint16_t y)
{
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

static uint16_t step_button_color(jog_screen_action_t action)
{
    if((action == JogScreenAction_Step01 && selected_step_mm == 0.1f) ||
       (action == JogScreenAction_Step1  && selected_step_mm == 1.0f) ||
       (action == JogScreenAction_Step10 && selected_step_mm == 10.0f))
        return UI_COLOR_ACCENT;

    return UI_COLOR_BUTTON_ALT;
}

static uint16_t feed_button_color(jog_screen_action_t action)
{
    if((action == JogScreenAction_FeedSlow   && selected_feed_mm_min == 200u) ||
       (action == JogScreenAction_FeedMedium && selected_feed_mm_min == 500u) ||
       (action == JogScreenAction_FeedFast   && selected_feed_mm_min == 1000u))
        return UI_COLOR_ACCENT;

    return UI_COLOR_BUTTON_ALT;
}

void jog_screen_draw(void)
{
    char step_text[20];
    char feed_text[20];
    snprintf(step_text,
             sizeof(step_text),
             "STEP  %s",
             selected_step_mm == 0.1f ? "0.1" : selected_step_mm == 10.0f ? "10" : "1.0");
    snprintf(feed_text, sizeof(feed_text), "FEED  %u", (unsigned)selected_feed_mm_min);

    ui_draw_panel((ui_rect_t){0, UI_CONTENT_TOP, 160, UI_CONTENT_H}, "POSITION");
    ui_draw_panel((ui_rect_t){160, UI_CONTENT_TOP, 160, UI_CONTENT_H}, "JOG PAD");
    ui_draw_panel((ui_rect_t){320, UI_CONTENT_TOP, 160, UI_CONTENT_H / 2}, "STEP");
    ui_draw_panel((ui_rect_t){320, UI_CONTENT_TOP + UI_CONTENT_H / 2, 160, UI_CONTENT_H / 2}, "FEED");

    tft_display_draw_text(46, 74, "X  0.000", UI_COLOR_TEXT, UI_COLOR_CARD, 2);
    tft_display_draw_text(46, 104, "Y  0.000", UI_COLOR_TEXT, UI_COLOR_CARD, 2);
    tft_display_draw_text(46, 134, "Z  0.000", UI_COLOR_TEXT, UI_COLOR_CARD, 2);
    tft_display_draw_text(52, 174, step_text, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    tft_display_draw_text(52, 194, feed_text, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    ui_draw_button((ui_rect_t){10, 202, 140, 28}, "HOME ALL", UI_COLOR_SUCCESS, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){10, 236, 140, 28}, "ZERO XYZ", UI_RGB565(176, 116, 38), UI_COLOR_TEXT, 1);

    ui_draw_button((ui_rect_t){222, 84, 36, 36}, "Y+", UI_COLOR_BUTTON, UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){222, 166, 36, 36}, "Y-", UI_COLOR_BUTTON, UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){180, 125, 36, 36}, "X-", UI_COLOR_BUTTON, UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){264, 125, 36, 36}, "X+", UI_COLOR_BUTTON, UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){222, 125, 36, 36}, "STOP", UI_COLOR_BUTTON_ALT, UI_COLOR_TEXT, 1);
    ui_draw_button((ui_rect_t){178, 219, 56, 30}, "Z+", UI_RGB565(62, 112, 176), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){246, 219, 56, 30}, "Z-", UI_RGB565(62, 112, 176), UI_COLOR_TEXT, 2);

    ui_draw_button((ui_rect_t){335, 94, 40, 32}, "0.1", step_button_color(JogScreenAction_Step01), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){380, 94, 40, 32}, "1.0", step_button_color(JogScreenAction_Step1), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){425, 94, 40, 32}, "10", step_button_color(JogScreenAction_Step10), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){335, 209, 40, 32}, "S", feed_button_color(JogScreenAction_FeedSlow), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){380, 209, 40, 32}, "M", feed_button_color(JogScreenAction_FeedMedium), UI_COLOR_TEXT, 2);
    ui_draw_button((ui_rect_t){425, 209, 40, 32}, "F", feed_button_color(JogScreenAction_FeedFast), UI_COLOR_TEXT, 2);
}

bool jog_screen_hit_test(uint16_t x, uint16_t y, jog_screen_action_t *action)
{
    for(uint8_t i = 0; i < sizeof(jog_buttons) / sizeof(jog_buttons[0]); i++) {
        if(rect_contains(jog_buttons[i].rect, x, y)) {
            if(action != NULL)
                *action = jog_buttons[i].action;
            return true;
        }
    }

    if(action != NULL)
        *action = JogScreenAction_None;
    return false;
}

void jog_screen_apply_selection(jog_screen_action_t action)
{
    switch(action) {
        case JogScreenAction_Step01:
            selected_step_mm = 0.1f;
            break;
        case JogScreenAction_Step1:
            selected_step_mm = 1.0f;
            break;
        case JogScreenAction_Step10:
            selected_step_mm = 10.0f;
            break;
        case JogScreenAction_FeedSlow:
            selected_feed_mm_min = 200u;
            break;
        case JogScreenAction_FeedMedium:
            selected_feed_mm_min = 500u;
            break;
        case JogScreenAction_FeedFast:
            selected_feed_mm_min = 1000u;
            break;
        default:
            break;
    }
}

float jog_screen_step_mm(void)
{
    return selected_step_mm;
}

uint16_t jog_screen_feed_mm_min(void)
{
    return selected_feed_mm_min;
}
