#include "app/status/status_provider.h"

namespace {
const char* machine_state_text(MachineOperationState state) {
    switch (state) {
        case MachineOperationState::Booting:
            return "BOOT";
        case MachineOperationState::Syncing:
            return "SYNC";
        case MachineOperationState::TeensyDisconnected:
            return "NO MCU";
        case MachineOperationState::Idle:
            return "IDLE";
        case MachineOperationState::Homing:
            return "HOME";
        case MachineOperationState::Jog:
            return "JOG";
        case MachineOperationState::Starting:
            return "START";
        case MachineOperationState::Running:
            return "RUN";
        case MachineOperationState::Hold:
            return "HOLD";
        case MachineOperationState::Fault:
            return "FAULT";
        case MachineOperationState::Estop:
            return "ESTOP";
        case MachineOperationState::CommsFault:
            return "COMMS ERR";
        case MachineOperationState::Uploading:
            return "UPLOAD";
    }

    return "--";
}
}

StatusProvider::StatusProvider(const MachineFsm& machine_fsm,
                               const JogStateMachine& jog_state_machine,
                               const JobStateMachine& job_state_machine,
                               const StorageService& storage_service,
                               const UsbCdcTransport& usb_transport)
    : machine_fsm_(machine_fsm),
      jog_state_machine_(jog_state_machine),
      job_state_machine_(job_state_machine),
      storage_service_(storage_service),
      usb_transport_(usb_transport) {}

StatusSnapshot StatusProvider::current() const {
    jog_state_machine_.format_xyz(xyz_text_, sizeof(xyz_text_));
    return StatusSnapshot{
        machine_state_text(machine_fsm_.state()),
        storage_service_.status_text(),
        usb_transport_.connected() ? "ON" : "OFF",
        nullptr,
        xyz_text_,
        "12:34",
    };
}
