#pragma once

#include <stdbool.h>

typedef enum {
    AppConfigStatus_Ready = 0,
    AppConfigStatus_MountMissing,
    AppConfigStatus_DirectoryError,
    AppConfigStatus_ReadError,
    AppConfigStatus_WriteError
} app_config_status_t;

app_config_status_t app_config_store_bootstrap(void);
const char *app_config_status_text(app_config_status_t status);
