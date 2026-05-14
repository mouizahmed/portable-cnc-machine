#include "machine/machine_status.h"

#include <stddef.h>
#include <string.h>

#include "grbl/system.h"
#include "grbl/state_machine.h"

static app_machine_mode_t map_state(sys_state_t state)
{
    if(state == STATE_IDLE)
        return AppMachineMode_Idle;

    if(state & STATE_ALARM)
        return AppMachineMode_Alarm;

    if(state & STATE_ESTOP)
        return AppMachineMode_Alarm;

    if(state & STATE_HOLD)
        return AppMachineMode_Holding;

    if(state & STATE_JOG)
        return AppMachineMode_Jogging;

    if(state & STATE_CYCLE)
        return AppMachineMode_RunningJob;

    return AppMachineMode_Idle;
}

void machine_status_snapshot(machine_status_snapshot_t *snapshot)
{
    if(snapshot == NULL)
        return;

    memset(snapshot, 0, sizeof(*snapshot));

    const sys_state_t state = state_get();
    float mpos[N_AXIS] = {0};

    system_convert_array_steps_to_mpos(mpos, sys.position);

    snapshot->mode = map_state(state);
    snapshot->mpos_x = mpos[X_AXIS];
    snapshot->mpos_y = mpos[Y_AXIS];
    snapshot->mpos_z = mpos[Z_AXIS];
}
