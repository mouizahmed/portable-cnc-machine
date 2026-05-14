#include "app/machine/machine_state_machine.h"

MachineState MachineStateMachine::state() const {
    return state_;
}

bool MachineStateMachine::handle_event(MachineEvent event) {
    switch (event) {
        case MachineEvent::StartCalibration:
            if (state_ != MachineState::Booting) {
                return false;
            }
            state_ = MachineState::Calibrating;
            return true;
        case MachineEvent::CalibrationCompleted:
            if (state_ != MachineState::Calibrating) {
                return false;
            }
            state_ = MachineState::Idle;
            return true;
        case MachineEvent::RunRequested:
            if (state_ != MachineState::Idle && state_ != MachineState::Hold) {
                return false;
            }
            state_ = MachineState::Running;
            return true;
        case MachineEvent::HoldRequested:
            if (state_ != MachineState::Running) {
                return false;
            }
            state_ = MachineState::Hold;
            return true;
        case MachineEvent::AlarmRaised:
            state_ = MachineState::Alarm;
            return true;
        case MachineEvent::EstopRaised:
            state_ = MachineState::Estop;
            return true;
        case MachineEvent::ResetToIdle:
            if (state_ != MachineState::Hold && state_ != MachineState::Alarm) {
                return false;
            }
            state_ = MachineState::Idle;
            return true;
    }

    return false;
}

void MachineStateMachine::set_state(MachineState state) {
    state_ = state;
}
