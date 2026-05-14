#include "ui/screens/files_screen.h"

#include <stddef.h>
#include <stdio.h>

#include "ui/components/ui_components.h"
#include "ui/display/tft_display.h"
#include "ui/ui_types.h"

static void format_file_size(uint32_t bytes, char *dest, size_t dest_size)
{
    if(dest == nullptr || dest_size == 0)
        return;

    if(bytes >= 1024UL * 1024UL) {
        const uint32_t whole = bytes / (1024UL * 1024UL);
        const uint32_t decimal = ((bytes % (1024UL * 1024UL)) * 10UL) / (1024UL * 1024UL);
        snprintf(dest, dest_size, "%lu.%lu MB", (unsigned long)whole, (unsigned long)decimal);
    } else if(bytes >= 1024UL) {
        const uint32_t whole = bytes / 1024UL;
        const uint32_t decimal = ((bytes % 1024UL) * 10UL) / 1024UL;
        snprintf(dest, dest_size, "%lu.%lu KB", (unsigned long)whole, (unsigned long)decimal);
    } else {
        snprintf(dest, dest_size, "%lu B", (unsigned long)bytes);
    }
}

bool files_screen_row_rect(uint8_t index, ui_rect_t *rect)
{
    if(rect == nullptr || index >= SD_GCODE_FILE_MAX_COUNT)
        return false;

    *rect = (ui_rect_t){10, (int16_t)(72 + index * 30), 300, 26};
    return true;
}

ui_rect_t files_screen_details_rect(void)
{
    return (ui_rect_t){320, UI_CONTENT_TOP, 160, UI_CONTENT_H};
}

void files_screen_draw_row(const sd_gcode_file_list_t *files, uint8_t index, int8_t selected_index)
{
    ui_rect_t row = {0};
    if(files == NULL || index >= files->count || !files_screen_row_rect(index, &row))
        return;

    const uint16_t fill = selected_index == (int8_t)index ? UI_COLOR_BUTTON : UI_COLOR_CARD;
    const uint16_t text = selected_index == (int8_t)index ? UI_COLOR_TEXT : UI_COLOR_TEXT;
    const uint16_t muted = selected_index == (int8_t)index ? UI_COLOR_TEXT : UI_COLOR_MUTED;

    tft_display_fill_rect(row, fill);
    tft_display_draw_rect(row, UI_COLOR_BORDER);
    tft_display_draw_text(row.x + 10, row.y + 9, files->names[index], text, fill, 1);
    tft_display_draw_text(row.x + row.w - 52, row.y + 9, "GCODE", muted, fill, 1);
}

void files_screen_draw_details(const sd_gcode_file_list_t *files, int8_t selected_index)
{
    const ui_rect_t details = files_screen_details_rect();

    ui_draw_panel(details, "DETAILS");

    if(files == NULL || !files->sd_ready) {
        tft_display_draw_text(336, 78, "SOURCE", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        tft_display_draw_text(336, 104, "CHECK CARD", UI_COLOR_MUTED, UI_COLOR_CARD, 1);
        return;
    }

    if(files->count == 0 || selected_index < 0 || selected_index >= (int8_t)files->count) {
        tft_display_draw_text(336, 78, "SOURCE: SD", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        tft_display_draw_text(336, 104, "FILES: 0", UI_COLOR_MUTED, UI_COLOR_CARD, 1);
        return;
    }

    tft_display_draw_text(336, 78, "PREVIEW", UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    tft_display_draw_text(336, 98, files->names[selected_index], UI_COLOR_TEXT, UI_COLOR_CARD, 1);
    char size_label[32];
    char size_line[48];
    format_file_size(files->sizes[selected_index], size_label, sizeof(size_label));
    snprintf(size_line, sizeof(size_line), "SIZE: %s", size_label);
    tft_display_draw_text(336, 122, size_line, UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    tft_display_draw_text(336, 196, "LOADED JOB", UI_COLOR_MUTED, UI_COLOR_CARD, 1);
    tft_display_draw_text(336, 216, "NONE", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
}

bool files_screen_hit_test_row(uint16_t x, uint16_t y, const sd_gcode_file_list_t *files, uint8_t *index)
{
    if(files == NULL || index == NULL || !files->sd_ready)
        return false;

    for(uint8_t i = 0; i < files->count && i < SD_GCODE_FILE_MAX_COUNT; i++) {
        ui_rect_t row = {0};
        if(files_screen_row_rect(i, &row) &&
           x >= row.x && x < row.x + row.w &&
           y >= row.y && y < row.y + row.h) {
            *index = i;
            return true;
        }
    }

    return false;
}

void files_screen_draw(const sd_gcode_file_list_t *files, int8_t selected_index)
{
    ui_draw_panel((ui_rect_t){0, UI_CONTENT_TOP, 320, UI_CONTENT_H}, "FILES");
    files_screen_draw_details(files, selected_index);

    if(files == NULL || !files->sd_ready) {
        tft_display_draw_text(20, 78, "SD NOT MOUNTED", UI_COLOR_WARNING, UI_COLOR_CARD, 1);
        return;
    }

    if(files->count == 0) {
        tft_display_draw_text(20, 78, "NO G-CODE FILES FOUND", UI_COLOR_TEXT, UI_COLOR_CARD, 1);
        tft_display_draw_text(20, 104, "CHECK STORAGE CARD / ROOT", UI_COLOR_MUTED, UI_COLOR_CARD, 1);
        return;
    }

    for(uint8_t i = 0; i < files->count && i < SD_GCODE_FILE_MAX_COUNT; i++)
        files_screen_draw_row(files, i, selected_index);

    if(files->truncated)
        tft_display_draw_text(124, 244, "MORE FILES ON SD", UI_COLOR_MUTED, UI_COLOR_CARD, 1);
}
