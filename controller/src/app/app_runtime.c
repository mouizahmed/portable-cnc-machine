#include "app/app_runtime.h"

#include "app/app_touch_service.h"
#include "app/app_ui_controller.h"
#include "app/app_ui_state.h"
#include "app_config/app_config_store.h"
#include "protocol/app_protocol.h"
#include "ui/ui_shell.h"

#include "grbl/messages.h"
#include "grbl/task.h"

#include <stddef.h>

void report_message(const char *msg, message_type_t type);

static void app_ui_refresh_task(void *data)
{
    (void)data;

    const uint8_t dirty_fields = app_ui_state_refresh();
    app_ui_controller_update_runtime_fields(dirty_fields);

    task_add_delayed(app_ui_refresh_task, NULL, 500);
}

void app_runtime_startup_task(void *data)
{
    (void)data;

    app_ui_state_init();
    ui_shell_set_runtime_state(app_ui_state_snapshot());

    app_config_status_t status = app_config_store_bootstrap();
    report_message(app_config_status_text(status),
                   status == AppConfigStatus_Ready ? Message_Info : Message_Warning);

    app_protocol_start();
    app_touch_service_start();
    task_add_delayed(app_ui_refresh_task, NULL, 500);
}
