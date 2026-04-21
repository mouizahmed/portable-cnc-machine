#include "services/portable_cnc_controller.h"

PortableCncController::PortableCncController(MachineFsm& machine,
                                             JogStateMachine& jog,
                                             JobStateMachine& jobs,
                                             MachineSettingsStore& machine_settings,
                                             StorageService& storage)
    : machine_(machine), jog_(jog), jobs_(jobs), machine_settings_(machine_settings), storage_(storage) {}

bool PortableCncController::begin_calibration() {
    return true;
}

bool PortableCncController::complete_calibration() {
    return true;
}

bool PortableCncController::poll_storage() {
    return storage_.poll(jobs_);
}

bool PortableCncController::refresh_job_files() {
    if (!can_load_file()) {
        return false;
    }
    return storage_.refresh_job_files(jobs_);
}

bool PortableCncController::force_storage_remount() {
    return storage_.force_remount(jobs_);
}

bool PortableCncController::apply_control(ControlCommand command) {
    switch (command) {
        case ControlCommand::Start:
            if (!can_run_loaded_job()) {
                return false;
            }
            return jobs_.handle_event(JobEvent::StartRun) &&
                   machine_.handle_event(MachineEvent::StartCmd);
        case ControlCommand::Pause:
            if (machine_.state() != MachineOperationState::Running) {
                return false;
            }
            return machine_.handle_event(MachineEvent::PauseCmd);
        case ControlCommand::Resume:
            if (machine_.state() != MachineOperationState::Hold) {
                return false;
            }
            return machine_.handle_event(MachineEvent::ResumeCmd);
    }

    return false;
}

bool PortableCncController::apply_jog_action(JogAction action) {
    if (!can_jog()) {
        return false;
    }
    return jog_.handle_action(action);
}

bool PortableCncController::handle_primary_action() {
    switch (primary_action()) {
        case PrimaryAction::Start:
            return apply_control(ControlCommand::Start);
        case PrimaryAction::Pause:
            return apply_control(ControlCommand::Pause);
        case PrimaryAction::Resume:
            return apply_control(ControlCommand::Resume);
        case PrimaryAction::LoadJob:
        case PrimaryAction::None:
            return false;
    }

    return false;
}

MachineOperationState PortableCncController::machine_state() const {
    return machine_.state();
}

const JogStateMachine& PortableCncController::jog() const {
    return jog_;
}

const JobStateMachine& PortableCncController::jobs() const {
    return jobs_;
}

const MachineSettings& PortableCncController::machine_settings() const {
    return machine_settings_.current();
}

StorageState PortableCncController::storage_state() const {
    return storage_.state();
}

const char* PortableCncController::storage_status_text() const {
    return storage_.status_text();
}

uint64_t PortableCncController::storage_free_bytes() const {
    return storage_.free_bytes();
}

bool PortableCncController::can_jog() const {
    return machine_.state() == MachineOperationState::Idle;
}

bool PortableCncController::can_load_file() const {
    return machine_.state() == MachineOperationState::Idle ||
           machine_.state() == MachineOperationState::TeensyDisconnected;
}

bool PortableCncController::can_run_loaded_job() const {
    return machine_.state() == MachineOperationState::Idle && jobs_.can_run();
}

bool PortableCncController::save_machine_settings(const MachineSettings& settings, const char** error_reason) {
    return machine_settings_.apply(settings, error_reason);
}

PrimaryAction PortableCncController::primary_action() const {
    switch (machine_.state()) {
        case MachineOperationState::Idle:
            return jobs_.has_loaded_job() ? PrimaryAction::Start : PrimaryAction::LoadJob;
        case MachineOperationState::Running:
            return PrimaryAction::Pause;
        case MachineOperationState::Hold:
            return PrimaryAction::Resume;
        case MachineOperationState::Booting:
        case MachineOperationState::Syncing:
        case MachineOperationState::TeensyDisconnected:
        case MachineOperationState::Homing:
        case MachineOperationState::Jog:
        case MachineOperationState::Starting:
        case MachineOperationState::Fault:
        case MachineOperationState::Estop:
        case MachineOperationState::CommsFault:
        case MachineOperationState::Uploading:
            return PrimaryAction::None;
    }

    return PrimaryAction::None;
}

const char* PortableCncController::primary_action_label() const {
    switch (primary_action()) {
        case PrimaryAction::LoadJob:
            return "LOAD JOB";
        case PrimaryAction::Start:
            return "START JOB";
        case PrimaryAction::Pause:
            return "PAUSE JOB";
        case PrimaryAction::Resume:
            return "RESUME JOB";
        case PrimaryAction::None:
            return "UNAVAILABLE";
    }

    return "UNAVAILABLE";
}
