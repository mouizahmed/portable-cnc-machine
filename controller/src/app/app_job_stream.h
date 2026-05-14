#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AppJobStreamStart_Ok = 0,
    AppJobStreamStart_SdNotMounted,
    AppJobStreamStart_NotIdle,
    AppJobStreamStart_FileOpenFailed,
    AppJobStreamStart_Failed
} app_job_stream_start_result_t;

app_job_stream_start_result_t app_job_stream_start(char *path);
const char *app_job_stream_start_reason(app_job_stream_start_result_t result);
bool app_job_stream_resume(void);
bool app_job_stream_pause(void);
bool app_job_stream_abort(void);
bool app_job_stream_current_line(uint32_t *line);

#ifdef __cplusplus
}
#endif
