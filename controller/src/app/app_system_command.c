#include "app/app_system_command.h"

#include "grbl/system.h"

status_code_t app_system_execute_line(char *line)
{
    return system_execute_line(line);
}
