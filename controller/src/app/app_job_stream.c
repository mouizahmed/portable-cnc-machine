#include "app/app_job_stream.h"

#include <stddef.h>

#include "fs_stream.h"
#include "grbl/errors.h"
#include "grbl/grbl.h"
#include "grbl/state_machine.h"

static app_job_stream_start_result_t map_stream_status(status_code_t status)
{
    switch(status) {
        case Status_OK:
            return AppJobStreamStart_Ok;
        case Status_SDNotMounted:
        case Status_FsNotMounted:
            return AppJobStreamStart_SdNotMounted;
        case Status_SystemGClock:
            return AppJobStreamStart_NotIdle;
        case Status_FileOpenFailed:
            return AppJobStreamStart_FileOpenFailed;
        default:
            return AppJobStreamStart_Failed;
    }
}

app_job_stream_start_result_t app_job_stream_start(char *path)
{
    return map_stream_status(stream_file(state_get(), path));
}

const char *app_job_stream_start_reason(app_job_stream_start_result_t result)
{
    switch(result) {
        case AppJobStreamStart_Ok:
            return "";
        case AppJobStreamStart_SdNotMounted:
            return "SD not mounted";
        case AppJobStreamStart_NotIdle:
            return "Controller is not idle";
        case AppJobStreamStart_FileOpenFailed:
            return "Could not open loaded file";
        default:
            return "Could not start SD job";
    }
}

bool app_job_stream_resume(void)
{
    return grbl.enqueue_realtime_command != NULL &&
           grbl.enqueue_realtime_command(CMD_CYCLE_START);
}

bool app_job_stream_pause(void)
{
    return grbl.enqueue_realtime_command != NULL &&
           grbl.enqueue_realtime_command(CMD_FEED_HOLD);
}

bool app_job_stream_abort(void)
{
    return grbl.enqueue_realtime_command != NULL &&
           grbl.enqueue_realtime_command(CMD_STOP);
}

bool app_job_stream_current_line(uint32_t *line)
{
    if(line == NULL)
        return false;

    stream_job_t *job = stream_get_job_info();
    if(job == NULL)
        return false;

    *line = (uint32_t)job->line_number;
    return true;
}
