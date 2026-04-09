#include "services/portable_cnc_controller.h"

PortableCncController::PortableCncController(MachineStateMachine& machine,
                                             JogStateMachine& jog,
                                             JobStateMachine& jobs,
                                             StorageService& storage)
    : machine_(machine), jog_(jog), jobs_(jobs), storage_(storage) {}

bool PortableCncController::begin_calibration() {
    return machine_.handle_event(MachineEvent::StartCalibration);
}

bool PortableCncController::complete_calibration() {
    return machine_.handle_event(MachineEvent::CalibrationCompleted);
}

bool PortableCncController::poll_storage() {
    return storage_.poll(jobs_);
}

bool PortableCncController::refresh_job_files() {
    if (!can_select_file()) {
        return false;
    }
    return storage_.refresh_job_files(jobs_);
}

bool PortableCncController::select_file(int16_t index) {
    if (!can_select_file()) {
        return false;
    }
    return jobs_.handle_event(JobEvent::SelectFile, index);
}

bool PortableCncController::apply_control(ControlCommand command) {
    switch (command) {
        case ControlCommand::Start:
            if (!can_run_selected_file()) {
                return false;
            }
            return jobs_.handle_event(JobEvent::StartRun) &&
                   machine_.handle_event(MachineEvent::RunRequested);
        case ControlCommand::Pause:
            if (machine_.state() != MachineState::Running) {
                return false;
            }
            return machine_.handle_event(MachineEvent::HoldRequested);
        case ControlCommand::Resume:
            if (machine_.state() != MachineState::Hold) {
                return false;
            }
            return machine_.handle_event(MachineEvent::RunRequested);
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

MachineState PortableCncController::machine_state() const {
    return machine_.state();
}

const JogStateMachine& PortableCncController::jog() const {
    return jog_;
}

const JobStateMachine& PortableCncController::jobs() const {
    return jobs_;
}

StorageState PortableCncController::storage_state() const {
    return storage_.state();
}

const char* PortableCncController::storage_status_text() const {
    return storage_.status_text();
}

bool PortableCncController::can_jog() const {
    return machine_.state() == MachineState::Idle;
}

bool PortableCncController::can_select_file() const {
    return machine_.state() == MachineState::Idle;
}

bool PortableCncController::can_run_selected_file() const {
    return machine_.state() == MachineState::Idle && jobs_.can_run();
}

PrimaryAction PortableCncController::primary_action() const {
    switch (machine_.state()) {
        case MachineState::Idle:
            return jobs_.has_selection() ? PrimaryAction::Start : PrimaryAction::LoadJob;
        case MachineState::Running:
            return PrimaryAction::Pause;
        case MachineState::Hold:
            return PrimaryAction::Resume;
        case MachineState::Booting:
        case MachineState::Calibrating:
        case MachineState::Alarm:
        case MachineState::Estop:
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
