#include "app_config/app_config_store.h"

#include <stddef.h>
#include <string.h>

#include "grbl/vfs.h"

#define APP_CONFIG_DIR      "/littlefs/app"
#define APP_CONFIG_FILE     "/littlefs/app/config.json"

static const char default_config[] =
    "{\n"
    "  \"schema\": 1,\n"
    "  \"touch\": {\n"
    "    \"calibrated\": false\n"
    "  },\n"
    "  \"ui\": {\n"
    "    \"theme\": \"default\",\n"
    "    \"brightness\": 100\n"
    "  },\n"
    "  \"machine\": {\n"
    "    \"profile\": \"portable-cnc\"\n"
    "  },\n"
    "  \"recentFiles\": [],\n"
    "  \"diagnostics\": {\n"
    "    \"bootCount\": 0\n"
    "  }\n"
    "}\n";

static bool directory_exists(const char *path)
{
    vfs_stat_t st = {0};

    return vfs_stat(path, &st) == 0 && st.st_mode.directory;
}

static bool file_exists(const char *path)
{
    vfs_stat_t st = {0};

    return vfs_stat(path, &st) == 0 && !st.st_mode.directory;
}

static bool ensure_directory(const char *path)
{
    return directory_exists(path) || vfs_mkdir(path) == 0 || directory_exists(path);
}

static app_config_status_t write_default_config(void)
{
    vfs_file_t *file = vfs_open(APP_CONFIG_FILE, "w");

    if(file == NULL)
        return AppConfigStatus_WriteError;

    const size_t len = strlen(default_config);
    const size_t written = vfs_write(default_config, 1, len, file);
    vfs_close(file);

    return written == len ? AppConfigStatus_Ready : AppConfigStatus_WriteError;
}

app_config_status_t app_config_store_bootstrap(void)
{
    if(!directory_exists("/littlefs"))
        return AppConfigStatus_MountMissing;

    if(!ensure_directory(APP_CONFIG_DIR))
        return AppConfigStatus_DirectoryError;

    if(!file_exists(APP_CONFIG_FILE))
        return write_default_config();

    vfs_file_t *file = vfs_open(APP_CONFIG_FILE, "r");

    if(file == NULL)
        return AppConfigStatus_ReadError;

    char marker[16] = {0};
    (void)vfs_read(marker, 1, sizeof(marker) - 1, file);
    vfs_close(file);

    return strstr(marker, "{") != NULL ? AppConfigStatus_Ready : AppConfigStatus_ReadError;
}

const char *app_config_status_text(app_config_status_t status)
{
    switch(status) {
        case AppConfigStatus_Ready:
            return "LittleFS app config ready";
        case AppConfigStatus_MountMissing:
            return "LittleFS mount missing";
        case AppConfigStatus_DirectoryError:
            return "LittleFS app config directory error";
        case AppConfigStatus_ReadError:
            return "LittleFS app config read error";
        case AppConfigStatus_WriteError:
            return "LittleFS app config write error";
        default:
            return "LittleFS app config unknown error";
    }
}
