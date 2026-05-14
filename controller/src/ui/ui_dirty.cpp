#include "ui/ui_dirty.h"

#include <stddef.h>

#define UI_DIRTY_MAX_RECTS 8

static ui_rect_t dirty_rects[UI_DIRTY_MAX_RECTS];
static uint8_t dirty_rect_count = 0;

static int16_t rect_right(ui_rect_t rect)
{
    return (int16_t)(rect.x + rect.w);
}

static int16_t rect_bottom(ui_rect_t rect)
{
    return (int16_t)(rect.y + rect.h);
}

static bool rect_valid(ui_rect_t rect)
{
    return rect.w > 0 && rect.h > 0;
}

static bool rect_touches_or_overlaps(ui_rect_t a, ui_rect_t b)
{
    return rect_valid(a) && rect_valid(b) &&
           a.x <= rect_right(b) && rect_right(a) >= b.x &&
           a.y <= rect_bottom(b) && rect_bottom(a) >= b.y;
}

static ui_rect_t rect_union(ui_rect_t a, ui_rect_t b)
{
    const int16_t x0 = a.x < b.x ? a.x : b.x;
    const int16_t y0 = a.y < b.y ? a.y : b.y;
    const int16_t x1 = rect_right(a) > rect_right(b) ? rect_right(a) : rect_right(b);
    const int16_t y1 = rect_bottom(a) > rect_bottom(b) ? rect_bottom(a) : rect_bottom(b);

    return (ui_rect_t){x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}

static ui_rect_t rect_clip_to_screen(ui_rect_t rect)
{
    int16_t x0 = rect.x;
    int16_t y0 = rect.y;
    int16_t x1 = rect_right(rect);
    int16_t y1 = rect_bottom(rect);

    if(x0 < 0)
        x0 = 0;
    if(y0 < 0)
        y0 = 0;
    if(x1 > UI_LCD_WIDTH)
        x1 = UI_LCD_WIDTH;
    if(y1 > UI_LCD_HEIGHT)
        y1 = UI_LCD_HEIGHT;

    return (ui_rect_t){x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}

void ui_dirty_reset(void)
{
    dirty_rect_count = 0;
}

bool ui_dirty_mark(ui_rect_t rect)
{
    rect = rect_clip_to_screen(rect);
    if(!rect_valid(rect))
        return false;

    for(uint8_t i = 0; i < dirty_rect_count; i++) {
        if(rect_touches_or_overlaps(dirty_rects[i], rect)) {
            dirty_rects[i] = rect_union(dirty_rects[i], rect);

            for(uint8_t j = 0; j < dirty_rect_count; j++) {
                if(i != j && rect_touches_or_overlaps(dirty_rects[i], dirty_rects[j])) {
                    dirty_rects[i] = rect_union(dirty_rects[i], dirty_rects[j]);
                    dirty_rects[j] = dirty_rects[dirty_rect_count - 1];
                    dirty_rect_count--;
                    j = 0;
                }
            }

            return true;
        }
    }

    if(dirty_rect_count >= UI_DIRTY_MAX_RECTS) {
        dirty_rects[0] = (ui_rect_t){0, 0, UI_LCD_WIDTH, UI_LCD_HEIGHT};
        dirty_rect_count = 1;
        return true;
    }

    dirty_rects[dirty_rect_count++] = rect;
    return true;
}

bool ui_dirty_mark_full(void)
{
    ui_dirty_reset();
    dirty_rects[0] = (ui_rect_t){0, 0, UI_LCD_WIDTH, UI_LCD_HEIGHT};
    dirty_rect_count = 1;
    return true;
}

bool ui_dirty_has_pending(void)
{
    return dirty_rect_count > 0;
}

uint8_t ui_dirty_count(void)
{
    return dirty_rect_count;
}

void ui_dirty_flush(ui_dirty_draw_rect_fn draw_rect, void *context)
{
    if(draw_rect == NULL)
        return;

    while(dirty_rect_count > 0) {
        const ui_rect_t rect = dirty_rects[0];
        dirty_rects[0] = dirty_rects[dirty_rect_count - 1];
        dirty_rect_count--;

        if(rect_valid(rect))
            draw_rect(rect, context);
    }
}
