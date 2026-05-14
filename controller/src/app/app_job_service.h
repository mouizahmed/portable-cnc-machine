#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool app_job_service_has_job(void);
bool app_job_service_is_running(void);
const char *app_job_service_name(void);
const char *app_job_service_path(void);
unsigned long app_job_service_total_lines(void);
bool app_job_service_load(const char *name, char *reason, size_t reason_size);
void app_job_service_unload(void);
void app_job_service_unload_if_name(const char *name);
void app_job_service_set_running(bool running);

#ifdef __cplusplus
}
#endif
