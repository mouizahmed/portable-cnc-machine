#include "app/status/status_provider.h"

namespace {
const char* machine_state_text(MachineState state) {
    switch (state) {
        case MachineState::Booting:
            return "BOOT";
        case MachineState::Calibrating:
            return "CAL";
        case MachineState::Idle:
            return "IDLE";
        case MachineState::Running:
            return "RUN";
        case MachineState::Hold:
            return "HOLD";
        case MachineState::Alarm:
            return "ALARM";
        case MachineState::Estop:
            return "ESTOP";
    }

    return "--";
}
}  // namespace

StatusProvider::StatusProvider(const MachineStateMachine& machine_state_machine, const JobStateMachine& job_state_machine)
    : machine_state_machine_(machine_state_machine), job_state_machine_(job_state_machine) {}

StatusSnapshot StatusProvider::current() const {
    const FileEntry* selected_file = job_state_machine_.selected_entry();
    return StatusSnapshot{
        machine_state_text(machine_state_machine_.state()),
        job_state_machine_.count() > 0 ? "READY" : "--",
        "--",
        selected_file != nullptr ? selected_file->tool_text : "--",
        "-- -- --",
        "12:34",
    };
}
