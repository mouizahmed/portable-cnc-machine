#include "app/app_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grbl/task.h"

void my_plugin_init(void)
{
    task_run_on_startup(app_runtime_startup_task, NULL);
}
