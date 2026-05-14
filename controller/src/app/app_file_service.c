#include "app/app_file_service.h"

#include <stdio.h>
#include <stdint.h>

#include "grbl/messages.h"

void report_message(const char *msg, message_type_t type);

static sd_gcode_file_list_t gcode_files = {0};
static int8_t selected_index = -1;

const sd_gcode_file_list_t *app_file_service_files(void)
{
    return &gcode_files;
}

void app_file_service_refresh(bool report)
{
    const bool ok = sd_gcode_files_refresh(&gcode_files);

    if(!ok || !gcode_files.sd_ready || gcode_files.count == 0)
        selected_index = -1;
    else if(selected_index < 0 || selected_index >= (int8_t)gcode_files.count)
        selected_index = 0;

    if(!report)
        return;

    static char message[96];

    if(!ok || !gcode_files.sd_ready) {
        report_message("Files: SD not mounted", Message_Warning);
        return;
    }

    snprintf(message, sizeof(message), "Files: scanned %u entries, %u G-code files",
             gcode_files.seen_count, gcode_files.count);
    report_message(message, Message_Info);

    if(gcode_files.count == 0 && gcode_files.seen_count > 0) {
        snprintf(message, sizeof(message), "Files: first entry %.48s", gcode_files.first_seen[0]);
        report_message(message, Message_Info);
    }
}

int8_t app_file_service_selected_index(void)
{
    return selected_index;
}

bool app_file_service_select_index(uint8_t index, int8_t *previous_index)
{
    if(index >= gcode_files.count)
        return false;

    if(previous_index != NULL)
        *previous_index = selected_index;

    if(selected_index == (int8_t)index)
        return false;

    selected_index = (int8_t)index;
    return true;
}
