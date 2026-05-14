#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JogScreenAction_None = 0,
    JogScreenAction_XMinus,
    JogScreenAction_XPlus,
    JogScreenAction_YMinus,
    JogScreenAction_YPlus,
    JogScreenAction_ZMinus,
    JogScreenAction_ZPlus,
    JogScreenAction_Stop,
    JogScreenAction_Step01,
    JogScreenAction_Step1,
    JogScreenAction_Step10,
    JogScreenAction_FeedSlow,
    JogScreenAction_FeedMedium,
    JogScreenAction_FeedFast
} jog_screen_action_t;

void jog_screen_draw(void);
bool jog_screen_hit_test(uint16_t x, uint16_t y, jog_screen_action_t *action);
void jog_screen_apply_selection(jog_screen_action_t action);
float jog_screen_step_mm(void);
uint16_t jog_screen_feed_mm_min(void);

#ifdef __cplusplus
}
#endif
