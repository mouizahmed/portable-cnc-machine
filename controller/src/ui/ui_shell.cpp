#include "ui/ui_shell.h"

#include <cstdio>
#include <stddef.h>

#include "ui/components/ui_components.h"
#include "ui/display/tft_display.h"
#include "ui/screens/files_screen.h"
#include "ui/screens/home_screen.h"
#include "ui/screens/jog_screen.h"
#include "ui/screens/run_screen.h"
#include "ui/screens/settings_screen.h"
#include "ui/screens/status_screen.h"
#include "ui/ui_dirty.h"

typedef struct {
    const char *label;
    ui_screen_t screen;
    ui_rect_t rect;
} nav_button_t;

typedef struct {
    ui_screen_t active_screen;
    const sd_gcode_file_list_t *files;
    int8_t selected_file_index;
} shell_render_context_t;

static const ui_runtime_state_t *runtime_state = NULL;

static const nav_button_t nav_buttons[] = {
    { "HOME",  UiScreen_Home,     {0,   274, 80, 46} },
    { "JOG",   UiScreen_Jog,      {80,  274, 80, 46} },
    { "FILES", UiScreen_Files,    {160, 274, 80, 46} },
    { "RUN",   UiScreen_Run,      {240, 274, 80, 46} },
    { "STAT",  UiScreen_Status,   {320, 274, 80, 46} },
    { "SET",   UiScreen_Settings, {400, 274, 80, 46} }
};

static void draw_status_bar(void)
{
    const char *machine = runtime_state != NULL ? runtime_state->machine_state : "IDLE";
    const char *storage = runtime_state != NULL ? runtime_state->sd_status : "SD";
    char line[48];
    snprintf(line, sizeof(line), "M:%s STR:%s USB:OK", machine, storage);

    tft_display_fill_rect((ui_rect_t){0, 0, UI_LCD_WIDTH, UI_TOP_BAR_H}, UI_COLOR_PANEL);
    tft_display_draw_rect((ui_rect_t){0, 0, UI_LCD_WIDTH, UI_TOP_BAR_H}, UI_COLOR_BORDER);
    tft_display_draw_text(12, 13, line, UI_COLOR_TEXT, UI_COLOR_PANEL, 2);
}

static void draw_nav_button(const nav_button_t *button, ui_screen_t active_screen)
{
    ui_draw_button(button->rect, button->label,
                   active_screen == button->screen ? UI_COLOR_BUTTON : UI_RGB565(36, 46, 58),
                   UI_COLOR_TEXT, 1);
}

static void draw_nav(ui_screen_t active_screen)
{
    tft_display_fill_rect((ui_rect_t){0, UI_LCD_HEIGHT - UI_BOTTOM_BAR_H, UI_LCD_WIDTH, UI_BOTTOM_BAR_H}, UI_COLOR_PANEL);
    tft_display_draw_rect((ui_rect_t){0, UI_LCD_HEIGHT - UI_BOTTOM_BAR_H, UI_LCD_WIDTH, UI_BOTTOM_BAR_H}, UI_COLOR_BORDER);

    for(uint8_t i = 0; i < sizeof(nav_buttons) / sizeof(nav_buttons[0]); i++)
        draw_nav_button(&nav_buttons[i], active_screen);
}

static const nav_button_t *nav_button_for_screen(ui_screen_t screen)
{
    for(uint8_t i = 0; i < sizeof(nav_buttons) / sizeof(nav_buttons[0]); i++) {
        if(nav_buttons[i].screen == screen)
            return &nav_buttons[i];
    }

    return NULL;
}

static void draw_content(ui_screen_t active_screen, const sd_gcode_file_list_t *files, int8_t selected_file_index)
{
    switch(active_screen) {
        case UiScreen_Jog:
            jog_screen_draw();
            break;
        case UiScreen_Files:
            files_screen_draw(files, selected_file_index);
            break;
        case UiScreen_Run:
            run_screen_draw(runtime_state);
            break;
        case UiScreen_Status:
            status_screen_draw(runtime_state);
            break;
        case UiScreen_Settings:
            settings_screen_draw();
            break;
        case UiScreen_Home:
        default:
            home_screen_draw();
            break;
    }
}

static void clear_content(void)
{
    tft_display_fill_rect((ui_rect_t){0, UI_CONTENT_TOP, UI_LCD_WIDTH, UI_CONTENT_H}, UI_COLOR_BACKGROUND);
}

static bool rect_intersects(ui_rect_t a, ui_rect_t b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static bool rect_equals(ui_rect_t a, ui_rect_t b)
{
    return a.x == b.x &&
           a.y == b.y &&
           a.w == b.w &&
           a.h == b.h;
}

static bool draw_screen_field_if_dirty(const shell_render_context_t *render_context, ui_rect_t rect)
{
    ui_rect_t field_rect = {0};
    const ui_screen_t screen = render_context->active_screen;

    if(screen == UiScreen_Run) {
        bool drew = false;
        const uint8_t fields[] = {
            UiRuntimeField_State,
            UiRuntimeField_File,
            UiRuntimeField_Time,
            UiRuntimeField_Progress,
            UiRuntimeField_Position,
            UiRuntimeField_Feed,
            UiRuntimeField_Spindle
        };

        for(uint8_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            if(run_screen_field_rect(fields[i], &field_rect) && rect_equals(rect, field_rect)) {
                run_screen_draw_fields(runtime_state, fields[i]);
                drew = true;
            }
        }

        return drew;
    }

    if(screen == UiScreen_Status) {
        bool drew = false;
        const uint8_t fields[] = {
            UiRuntimeField_State,
            UiRuntimeField_Position,
            UiRuntimeField_Storage
        };

        for(uint8_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            if(status_screen_field_rect(fields[i], &field_rect) && rect_equals(rect, field_rect)) {
                status_screen_draw_fields(runtime_state, fields[i]);
                drew = true;
            }
        }

        return drew;
    }

    if(screen == UiScreen_Files) {
        bool drew = false;
        ui_rect_t file_rect = {0};

        if(rect_equals(rect, files_screen_details_rect())) {
            files_screen_draw_details(render_context->files, render_context->selected_file_index);
            drew = true;
        }

        for(uint8_t i = 0; i < SD_GCODE_FILE_MAX_COUNT; i++) {
            if(files_screen_row_rect(i, &file_rect) && rect_equals(rect, file_rect)) {
                files_screen_draw_row(render_context->files, i, render_context->selected_file_index);
                drew = true;
            }
        }

        return drew;
    }

    return false;
}

static void mark_nav_button_dirty(ui_screen_t screen)
{
    const nav_button_t *button = nav_button_for_screen(screen);
    if(button != NULL)
        ui_dirty_mark(button->rect);
}

static void render_dirty_rect(ui_rect_t rect, void *context)
{
    const shell_render_context_t *render_context = (const shell_render_context_t *)context;
    const ui_rect_t top_bar = {0, 0, UI_LCD_WIDTH, UI_TOP_BAR_H};
    const ui_rect_t content = {0, UI_CONTENT_TOP, UI_LCD_WIDTH, UI_CONTENT_H};
    const ui_rect_t nav = {0, UI_LCD_HEIGHT - UI_BOTTOM_BAR_H, UI_LCD_WIDTH, UI_BOTTOM_BAR_H};

    if(rect_intersects(rect, top_bar))
        draw_status_bar();

    if(rect_intersects(rect, content)) {
        if(draw_screen_field_if_dirty(render_context, rect))
            return;

        clear_content();
        draw_content(render_context->active_screen, render_context->files, render_context->selected_file_index);
    }

    if(rect_intersects(rect, nav)) {
        if(rect.x <= nav.x && rect.y <= nav.y &&
           rect.x + rect.w >= nav.x + nav.w &&
           rect.y + rect.h >= nav.y + nav.h)
            draw_nav(render_context->active_screen);
        else
            for(uint8_t i = 0; i < sizeof(nav_buttons) / sizeof(nav_buttons[0]); i++) {
                if(rect_intersects(rect, nav_buttons[i].rect))
                    draw_nav_button(&nav_buttons[i], render_context->active_screen);
            }
    }
}

static void flush_shell(ui_screen_t active_screen, const sd_gcode_file_list_t *files, int8_t selected_file_index)
{
    shell_render_context_t context = {
        .active_screen = active_screen,
        .files = files,
        .selected_file_index = selected_file_index
    };

    ui_dirty_flush(render_dirty_rect, &context);
}

void ui_shell_show_calibration_target(uint8_t index, uint16_t x, uint16_t y)
{
    tft_display_fill_screen(UI_COLOR_BACKGROUND);
    tft_display_draw_text(16, 14, "Touch Calibration", UI_COLOR_TEXT, UI_COLOR_BACKGROUND, 2);
    tft_display_draw_text(16, 48, "Tap target", UI_COLOR_MUTED, UI_COLOR_BACKGROUND, 2);
    tft_display_draw_text(158, 48, index == 0 ? "1 of 4" : index == 1 ? "2 of 4" : index == 2 ? "3 of 4" : "4 of 4",
                          UI_COLOR_MUTED, UI_COLOR_BACKGROUND, 2);
    tft_display_draw_circle(x, y, 18, UI_COLOR_TEXT);
    tft_display_draw_circle(x, y, 10, UI_COLOR_ACCENT);
    tft_display_draw_hline(x - 24, y, 48, UI_COLOR_ACCENT);
    tft_display_draw_vline(x, y - 24, 48, UI_COLOR_ACCENT);
}

void ui_shell_show_calibration_done(void)
{
    tft_display_fill_screen(UI_COLOR_BACKGROUND);
    tft_display_draw_text(24, 96, "Calibration saved", UI_COLOR_ACCENT, UI_COLOR_BACKGROUND, 3);
    tft_display_draw_text(24, 148, "Touch is ready", UI_COLOR_MUTED, UI_COLOR_BACKGROUND, 2);
}

void ui_shell_show(ui_screen_t active_screen, const sd_gcode_file_list_t *files, int8_t selected_file_index)
{
    tft_display_fill_screen(UI_COLOR_BACKGROUND);
    ui_dirty_reset();
    ui_dirty_mark((ui_rect_t){0, 0, UI_LCD_WIDTH, UI_TOP_BAR_H});
    ui_dirty_mark((ui_rect_t){0, UI_CONTENT_TOP, UI_LCD_WIDTH, UI_CONTENT_H});
    ui_dirty_mark((ui_rect_t){0, UI_LCD_HEIGHT - UI_BOTTOM_BAR_H, UI_LCD_WIDTH, UI_BOTTOM_BAR_H});
    flush_shell(active_screen, files, selected_file_index);
}

void ui_shell_set_runtime_state(const ui_runtime_state_t *state)
{
    runtime_state = state;
}

void ui_shell_update_runtime_fields(ui_screen_t active_screen, uint8_t fields)
{
    ui_rect_t rect = {0};

    ui_dirty_reset();

    if(fields & (UiRuntimeField_State | UiRuntimeField_Storage))
        ui_dirty_mark((ui_rect_t){0, 0, UI_LCD_WIDTH, UI_TOP_BAR_H});

    if(active_screen == UiScreen_Run) {
        const uint8_t run_fields[] = {
            UiRuntimeField_State,
            UiRuntimeField_File,
            UiRuntimeField_Time,
            UiRuntimeField_Progress,
            UiRuntimeField_Position,
            UiRuntimeField_Feed,
            UiRuntimeField_Spindle
        };

        for(uint8_t i = 0; i < sizeof(run_fields) / sizeof(run_fields[0]); i++) {
            if((fields & run_fields[i]) && run_screen_field_rect(run_fields[i], &rect))
                ui_dirty_mark(rect);
        }
    } else if(active_screen == UiScreen_Status) {
        const uint8_t status_fields[] = {
            UiRuntimeField_State,
            UiRuntimeField_Position,
            UiRuntimeField_Storage
        };

        for(uint8_t i = 0; i < sizeof(status_fields) / sizeof(status_fields[0]); i++) {
            if((fields & status_fields[i]) && status_screen_field_rect(status_fields[i], &rect))
                ui_dirty_mark(rect);
        }
    }

    flush_shell(active_screen, NULL, -1);
}

void ui_shell_show_screen_change(ui_screen_t previous_screen,
                                 ui_screen_t active_screen,
                                 const sd_gcode_file_list_t *files,
                                 int8_t selected_file_index)
{
    ui_dirty_reset();
    ui_dirty_mark((ui_rect_t){0, UI_CONTENT_TOP, UI_LCD_WIDTH, UI_CONTENT_H});
    mark_nav_button_dirty(previous_screen);
    mark_nav_button_dirty(active_screen);
    flush_shell(active_screen, files, selected_file_index);
}

void ui_shell_update_file_selection(const sd_gcode_file_list_t *files,
                                    int8_t previous_index,
                                    int8_t selected_index)
{
    ui_rect_t rect = {0};

    ui_dirty_reset();

    if(previous_index >= 0 && files_screen_row_rect((uint8_t)previous_index, &rect))
        ui_dirty_mark(rect);

    if(selected_index >= 0 && files_screen_row_rect((uint8_t)selected_index, &rect))
        ui_dirty_mark(rect);

    ui_dirty_mark(files_screen_details_rect());
    flush_shell(UiScreen_Files, files, selected_index);
}

void ui_shell_update_touch(ui_screen_t active_screen,
                           const sd_gcode_file_list_t *files,
                           uint16_t x,
                           uint16_t y,
                           const char *label)
{
    ui_shell_show(active_screen, files, -1);

    (void)x;
    (void)y;
    (void)label;
}
