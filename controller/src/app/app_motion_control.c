#include "app/app_motion_control.h"

#include <math.h>
#include <stdio.h>

#include "grbl/core_handlers.h"
#include "grbl/errors.h"
#include "grbl/grbl.h"
#include "grbl/system.h"

static app_motion_result_t map_status(status_code_t status)
{
    switch(status) {
        case Status_OK:
            return AppMotionResult_Ok;
        case Status_IdleError:
        case Status_SystemGClock:
            return AppMotionResult_NotIdle;
        case Status_HomingRequired:
            return AppMotionResult_HomingRequired;
        case Status_LimitsEngaged:
        case Status_SoftLimitError:
        case Status_TravelExceeded:
            return AppMotionResult_Limit;
        case Status_InvalidJogCommand:
        case Status_GcodeNoAxisWords:
        case Status_GcodeValueWordMissing:
        case Status_BadNumberFormat:
            return AppMotionResult_InvalidDistance;
        default:
            return AppMotionResult_Failed;
    }
}

static char app_axis_letter(uint8_t axis)
{
    switch(axis) {
        case 0:
            return 'X';
        case 1:
            return 'Y';
        case 2:
            return 'Z';
        default:
            return '\0';
    }
}

app_motion_result_t app_motion_jog(uint8_t axis, float distance_mm, uint16_t feed_mm_min)
{
    const char letter = app_axis_letter(axis);
    if(letter == '\0')
        return AppMotionResult_InvalidAxis;

    if(!isfinite(distance_mm) || distance_mm == 0.0f)
        return AppMotionResult_InvalidDistance;

    if(feed_mm_min == 0)
        return AppMotionResult_InvalidFeed;

    int32_t distance_milli = (int32_t)(distance_mm * 1000.0f + (distance_mm >= 0.0f ? 0.5f : -0.5f));
    if(distance_milli == 0)
        return AppMotionResult_InvalidDistance;

    const char sign = distance_milli < 0 ? '-' : '\0';
    uint32_t abs_milli = (uint32_t)(distance_milli < 0 ? -distance_milli : distance_milli);
    const uint32_t whole = abs_milli / 1000u;
    const uint32_t frac = abs_milli % 1000u;

    char line[64];
    int written = snprintf(line,
                           sizeof(line),
                           sign == '-'
                               ? "$J=G91G21%c-%lu.%03luF%u"
                               : "$J=G91G21%c%lu.%03luF%u",
                           letter,
                           (unsigned long)whole,
                           (unsigned long)frac,
                           (unsigned)feed_mm_min);
    if(written <= 0 || (size_t)written >= sizeof(line))
        return AppMotionResult_Failed;

    return map_status(system_execute_line(line));
}

app_motion_result_t app_motion_jog_cancel(void)
{
    if(grbl.enqueue_realtime_command != NULL &&
       grbl.enqueue_realtime_command(CMD_JOG_CANCEL))
        return AppMotionResult_Ok;

    return AppMotionResult_Failed;
}

const char *app_motion_result_reason(app_motion_result_t result)
{
    switch(result) {
        case AppMotionResult_Ok:
            return "";
        case AppMotionResult_InvalidAxis:
            return "Invalid jog axis";
        case AppMotionResult_InvalidDistance:
            return "Invalid jog distance";
        case AppMotionResult_InvalidFeed:
            return "Invalid jog feed";
        case AppMotionResult_NotIdle:
            return "Controller is not idle";
        case AppMotionResult_HomingRequired:
            return "Home machine to continue";
        case AppMotionResult_Limit:
            return "Jog target exceeds limits";
        default:
            return "Jog failed";
    }
}
