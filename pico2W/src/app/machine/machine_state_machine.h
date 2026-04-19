#pragma once

#include "core/state_types.h"

enum class MachineEvent : uint8_t {
    StartCalibration,
    CalibrationCompleted,
    RunRequested,
    HoldRequested,
    AlarmRaised,
    EstopRaised,
    ResetToIdle,
};

class MachineStateMachine {
public:
    MachineState state() const;
    bool handle_event(MachineEvent event);
    void set_state(MachineState state);

private:
    MachineState state_ = MachineState::Booting;
};
