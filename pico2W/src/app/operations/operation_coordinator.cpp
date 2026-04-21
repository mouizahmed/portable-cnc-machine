#include "app/operations/operation_coordinator.h"

namespace {
RequestDecision accept() {
    return {RequestDecisionType::AcceptNow};
}

RequestDecision preempt() {
    return {RequestDecisionType::PreemptAndAccept};
}

RequestDecision suppress_background() {
    return {RequestDecisionType::SuppressBackgroundAndAccept};
}

RequestDecision busy() {
    return {RequestDecisionType::RejectBusy};
}

RequestDecision invalid() {
    return {RequestDecisionType::RejectInvalidState};
}

bool storage_mounted(StorageState state) {
    return state == StorageState::Mounted;
}

bool worker_foreground_idle(const Core1WorkerSnapshot& worker) {
    return !worker.has_foreground_work;
}

RequestDecision accept_or_suppress_background(const Core1WorkerSnapshot& worker) {
    return worker.has_background_only_work ? suppress_background() : accept();
}
}  // namespace

const char* request_decision_text(RequestDecisionType type) {
    switch (type) {
        case RequestDecisionType::AcceptNow:             return "ACCEPT_NOW";
        case RequestDecisionType::Queue:                 return "QUEUE";
        case RequestDecisionType::RejectBusy:            return "REJECT_BUSY";
        case RequestDecisionType::RejectInvalidState:    return "REJECT_INVALID_STATE";
        case RequestDecisionType::PreemptAndAccept:      return "PREEMPT_AND_ACCEPT";
        case RequestDecisionType::AbortCurrentAndAccept: return "ABORT_CURRENT_AND_ACCEPT";
        case RequestDecisionType::SuppressBackgroundAndAccept: return "SUPPRESS_BACKGROUND_AND_ACCEPT";
        case RequestDecisionType::CoalesceWithExisting:  return "COALESCE_WITH_EXISTING";
    }
    return "UNKNOWN";
}

RequestDecision OperationCoordinator::decide(const OperationRequest& request,
                                             const MachineFsm& machine,
                                             const StorageTransferStateMachine& storage,
                                             JobStreamState stream,
                                             const Core1WorkerSnapshot& worker,
                                             StorageState storage_state) const {
    const MachineOperationState machine_state = machine.state();
    const CapsFlags caps = machine.caps();

    switch (request.type) {
        case OperationRequestType::Estop:
            return preempt();

        case OperationRequestType::UploadAbort:
            return storage.is_upload() ? preempt() : accept();

        case OperationRequestType::DownloadAbort:
            return storage.is_download() ? preempt() : accept();

        case OperationRequestType::JobAbort:
            return caps.job_abort ? preempt() : invalid();

        case OperationRequestType::JobHold:
            return caps.job_pause ? preempt() : invalid();

        case OperationRequestType::JogCancel:
            return machine_state == MachineOperationState::Jog ? preempt() : accept();

        case OperationRequestType::Reset:
            return caps.reset ? preempt() : invalid();

        case OperationRequestType::JobResume:
            return caps.job_resume ? accept() : invalid();

        case OperationRequestType::HomeAll:
        case OperationRequestType::Jog:
        case OperationRequestType::ZeroAll:
            if (storage.is_active() || stream_active(stream)) {
                return busy();
            }
            return caps.motion ? accept() : invalid();

        case OperationRequestType::JobStart:
            if (!caps.job_start) {
                return invalid();
            }
            if (storage.is_active() || stream_active(stream) || !worker_foreground_idle(worker)) {
                return busy();
            }
            return accept_or_suppress_background(worker);

        case OperationRequestType::FileUnload:
            if (storage.is_active() || stream_active(stream)) {
                return busy();
            }
            return machine_allows_storage(machine_state) ? accept() : invalid();

        case OperationRequestType::FileList:
        case OperationRequestType::FileLoad:
        case OperationRequestType::FileDelete:
        case OperationRequestType::UploadBegin:
        case OperationRequestType::DownloadBegin:
            if (!storage_mounted(storage_state) || !machine.sd_mounted()) {
                return invalid();
            }
            if (storage.is_active() || stream_active(stream) || !worker_foreground_idle(worker)) {
                return busy();
            }
            return machine_allows_storage(machine_state)
                ? accept_or_suppress_background(worker)
                : invalid();

        case OperationRequestType::SettingsSave:
        case OperationRequestType::CalibrationSave:
            if (storage.is_active() || stream_active(stream)) {
                return busy();
            }
            if (!worker_foreground_idle(worker)) {
                return busy();
            }
            return accept_or_suppress_background(worker);
    }

    return invalid();
}

bool OperationCoordinator::stream_active(JobStreamState stream) {
    return stream == JobStreamState::Preparing ||
           stream == JobStreamState::Beginning ||
           stream == JobStreamState::Streaming ||
           stream == JobStreamState::PausedByHold ||
           stream == JobStreamState::Cancelling;
}

bool OperationCoordinator::machine_allows_storage(MachineOperationState state) {
    return state == MachineOperationState::Idle ||
           state == MachineOperationState::TeensyDisconnected;
}
