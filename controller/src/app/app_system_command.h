#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "grbl/errors.h"

status_code_t app_system_execute_line(char *line);

#ifdef __cplusplus
}
#endif
