#include "storage/sd_gcode_files.h"

#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "sdcard.h"
#include "grbl/vfs.h"

static bool extension_matches(const char *name, const char *extension)
{
    const size_t name_len = strlen(name);
    const size_t ext_len = strlen(extension);

    if(name_len <= ext_len)
        return false;

    const char *candidate = name + name_len - ext_len;

    for(size_t i = 0; i < ext_len; i++) {
        if(tolower((unsigned char)candidate[i]) != tolower((unsigned char)extension[i]))
            return false;
    }

    return true;
}

static bool is_gcode_file(const vfs_dirent_t *entry)
{
    if(entry == NULL || entry->st_mode.directory || entry->name[0] == '\0')
        return false;

    return extension_matches(entry->name, ".gcode") ||
           extension_matches(entry->name, ".nc") ||
           extension_matches(entry->name, ".tap") ||
           extension_matches(entry->name, ".ngc") ||
           extension_matches(entry->name, ".cnc") ||
           extension_matches(entry->name, ".gc");
}

static void copy_file_name(char *dest, const char *source)
{
    uint8_t i = 0;

    while(i < SD_GCODE_FILE_NAME_MAX - 1 && source[i] != '\0') {
        dest[i] = source[i];
        i++;
    }

    dest[i] = '\0';
}

bool sd_gcode_files_refresh(sd_gcode_file_list_t *list)
{
    if(list == NULL)
        return false;

    memset(list, 0, sizeof(*list));

#if FS_ENABLE & FS_SDCARD
    if(sdcard_getfs() == NULL)
        return false;
#endif

    vfs_dir_t *dir = vfs_opendir("/");
    if(dir == NULL)
        return false;

    list->sd_ready = true;

    vfs_dirent_t *entry = NULL;
    while((entry = vfs_readdir(dir)) != NULL && entry->name[0] != '\0') {
        if(!entry->st_mode.directory) {
            if(list->seen_count < 3)
                copy_file_name(list->first_seen[list->seen_count], entry->name);
            if(list->seen_count < UINT8_MAX)
                list->seen_count++;
        }

        if(!is_gcode_file(entry))
            continue;

        if(list->count >= SD_GCODE_FILE_MAX_COUNT) {
            list->truncated = true;
            continue;
        }

        copy_file_name(list->names[list->count], entry->name);
        list->sizes[list->count] = entry->size > UINT32_MAX ? UINT32_MAX : (uint32_t)entry->size;
        list->count++;
    }

    vfs_closedir(dir);

    return true;
}
