#include "app/app_job_service.h"

#include <string.h>

#include "protocol/protocol_defs.h"
#include "grbl/vfs.h"

static bool loaded = false;
static bool running = false;
static char loaded_name[PCNC_MAX_FILENAME_BYTES] = {0};
static char loaded_path[PCNC_MAX_FILENAME_BYTES + 2] = {0};
static unsigned long loaded_total_lines = 0;

static void copy_fixed(char *dest, size_t dest_size, const char *src)
{
    if(dest == NULL || dest_size == 0)
        return;

    memset(dest, 0, dest_size);

    if(src == NULL)
        return;

    size_t len = 0;
    while(len < dest_size - 1 && src[len] != '\0')
        len++;

    memcpy(dest, src, len);
}

static bool build_sd_path(char *dest, size_t dest_size, const char *name)
{
    if(dest == NULL || dest_size < 2 || name == NULL || name[0] == '\0')
        return false;

    if(strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
        return false;

    const size_t name_len = strlen(name);
    if(name_len + 2 > dest_size)
        return false;

    dest[0] = '/';
    memcpy(dest + 1, name, name_len + 1);
    return true;
}

static void set_reason(char *reason, size_t reason_size, const char *text)
{
    if(reason == NULL || reason_size == 0)
        return;

    copy_fixed(reason, reason_size, text);
}

static unsigned long count_file_lines(const char *path)
{
    vfs_file_t *file = vfs_open(path, "r");
    if(file == NULL)
        return 0;

    unsigned long lines = 0;
    bool saw_any = false;
    bool last_was_newline = false;
    uint8_t buffer[256];

    while(!vfs_eof(file)) {
        const size_t read = vfs_read(buffer, 1, sizeof(buffer), file);
        if(read == 0)
            break;

        saw_any = true;
        for(size_t i = 0; i < read; i++) {
            if(buffer[i] == '\n') {
                lines++;
                last_was_newline = true;
            } else if(buffer[i] != '\r') {
                last_was_newline = false;
            }
        }
    }

    vfs_close(file);

    if(saw_any && !last_was_newline)
        lines++;

    return lines;
}

bool app_job_service_has_job(void)
{
    return loaded;
}

bool app_job_service_is_running(void)
{
    return running;
}

const char *app_job_service_name(void)
{
    return loaded ? loaded_name : "";
}

const char *app_job_service_path(void)
{
    return loaded ? loaded_path : "";
}

unsigned long app_job_service_total_lines(void)
{
    return loaded ? loaded_total_lines : 0;
}

bool app_job_service_load(const char *name, char *reason, size_t reason_size)
{
    char path[sizeof(loaded_path)] = {0};
    if(!build_sd_path(path, sizeof(path), name)) {
        set_reason(reason, reason_size, "Invalid filename");
        return false;
    }

    vfs_stat_t stat = {0};
    if(vfs_stat(path, &stat) != 0 || stat.st_mode.directory) {
        set_reason(reason, reason_size, "File not found");
        return false;
    }

    copy_fixed(loaded_name, sizeof(loaded_name), name);
    copy_fixed(loaded_path, sizeof(loaded_path), path);
    loaded_total_lines = count_file_lines(path);
    loaded = true;
    set_reason(reason, reason_size, "");
    return true;
}

void app_job_service_unload(void)
{
    running = false;
    loaded = false;
    loaded_name[0] = '\0';
    loaded_path[0] = '\0';
    loaded_total_lines = 0;
}

void app_job_service_set_running(bool is_running)
{
    running = is_running;
}

void app_job_service_unload_if_name(const char *name)
{
    if(!loaded || name == NULL)
        return;

    if(strcmp(loaded_name, name) == 0)
        app_job_service_unload();
}
