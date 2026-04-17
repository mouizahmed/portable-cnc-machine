#include "app/machine/machine_fsm.h"

MachineFsm::MachineFsm() = default;

bool MachineFsm::transition(MachineOperationState next) {
    if (state_ == next) return false;
    state_ = next;
    on_enter(next);
    return true;
}

void MachineFsm::on_enter(MachineOperationState s) {
    switch (s) {
        case MachineOperationState::Starting:
            job_stream_complete_ = false;
            abort_pending_       = false;
            break;
        case MachineOperationState::Idle:
            job_session_active_  = false;
            abort_pending_       = false;
            job_stream_complete_ = false;
            hold_complete_       = false;
            break;
        case MachineOperationState::Fault:
        case MachineOperationState::CommsFault:
            job_session_active_  = false;
            abort_pending_       = false;
            job_stream_complete_ = false;
            hold_complete_       = false;
            all_axes_homed_      = false;
            break;
        case MachineOperationState::Estop:
            job_session_active_  = false;
            abort_pending_       = false;
            job_stream_complete_ = false;
            hold_complete_       = false;
            all_axes_homed_      = false;
            upload_active_       = false;
            break;
        case MachineOperationState::TeensyDisconnected:
            job_session_active_  = false;
            abort_pending_       = false;
            job_stream_complete_ = false;
            hold_complete_       = false;
            teensy_connected_    = false;
            all_axes_homed_      = false;
            break;
        case MachineOperationState::Hold:
            hold_complete_ = false;
            break;
        case MachineOperationState::Syncing:
            hold_complete_ = false;
            break;
        default:
            break;
    }
}

bool MachineFsm::handle_grbl_idle() {
    if (!job_session_active_) {
        return transition(MachineOperationState::Idle);
    }
    if (abort_pending_) {
        // abort confirmed
        return transition(MachineOperationState::Idle);
    }
    if (job_stream_complete_) {
        // job complete â€” caller will emit @EVENT JOB_COMPLETE
        return transition(MachineOperationState::Idle);
    }
    // Between motion segments â€” stay
    return false;
}

void MachineFsm::notify_stream_complete() {
    job_stream_complete_ = true;
}

SafetyLevel MachineFsm::safety() const {
    if (state_ == MachineOperationState::Estop)    return SafetyLevel::Critical;
    if (state_ == MachineOperationState::Running  ||
        state_ == MachineOperationState::Homing   ||
        state_ == MachineOperationState::Jog      ||
        state_ == MachineOperationState::Starting)  return SafetyLevel::Monitoring;
    return SafetyLevel::Safe;
}

CapsFlags MachineFsm::caps() const {
    CapsFlags c{};
    const auto s = state_;
    c.motion      = (s == MachineOperationState::Idle);
    c.probe       = (s == MachineOperationState::Idle) && all_axes_homed_;
    c.spindle     = (s == MachineOperationState::Idle ||
                     s == MachineOperationState::Running ||
                     s == MachineOperationState::Hold);
    c.file_load = (s == MachineOperationState::Idle) && sd_mounted_;
    c.job_start   = (s == MachineOperationState::Idle) && job_loaded_ &&
                    teensy_connected_ && all_axes_homed_;
    c.job_pause   = (s == MachineOperationState::Running);
    c.job_resume  = (s == MachineOperationState::Hold) && hold_complete_;
    c.job_abort   = (s == MachineOperationState::Running ||
                     s == MachineOperationState::Hold ||
                     s == MachineOperationState::Starting);
    c.overrides   = (s == MachineOperationState::Running ||
                     s == MachineOperationState::Hold);
    c.reset       = (s == MachineOperationState::Fault) ||
                    (s == MachineOperationState::Estop && !hw_estop_active_);
    return c;
}

bool MachineFsm::handle_event(MachineEvent ev) {
    // SD card events are handled in any state
    if (ev == MachineEvent::SdMounted) {
        if (sd_mounted_) return false;
        sd_mounted_ = true;
        return true;
    }
    if (ev == MachineEvent::SdRemoved) {
        bool changed = sd_mounted_ || job_loaded_;
        sd_mounted_ = false;
        job_loaded_ = false;
        return changed;
    }

    // E-stop GPIO â€” handled in almost any state
    if (ev == MachineEvent::HwEstopAsserted) {
        hw_estop_active_ = true;
        if (state_ != MachineOperationState::Estop)
            return transition(MachineOperationState::Estop);
        return true; // flag changed even if state didn't
    }
    if (ev == MachineEvent::HwEstopCleared) {
        if (!hw_estop_active_) return false;
        hw_estop_active_ = false;
        return true; // flag changed; caps may change (reset becomes available)
    }

    switch (state_) {

        // â”€â”€ BOOTING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Booting:
            switch (ev) {
                case MachineEvent::TeensyConnected:
                    teensy_connected_ = true;
                    return transition(MachineOperationState::Syncing);
                case MachineEvent::BootTimeout:
                    return transition(MachineOperationState::TeensyDisconnected);
                case MachineEvent::JobLoaded:
                    if (job_loaded_) return false;
                    job_loaded_ = true; return true;
                case MachineEvent::JobUnloaded:
                    if (!job_loaded_) return false;
                    job_loaded_ = false; return true;
                default: return false;
            }

        // â”€â”€ SYNCING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Syncing:
            switch (ev) {
                case MachineEvent::GrblIdle:        return transition(MachineOperationState::Idle);
                case MachineEvent::GrblAlarm:       return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:       return transition(MachineOperationState::Estop);
                case MachineEvent::GrblHoming:      return transition(MachineOperationState::Homing);
                case MachineEvent::GrblCycle:       return transition(MachineOperationState::Running);
                case MachineEvent::GrblHoldPending: return transition(MachineOperationState::Hold);
                case MachineEvent::GrblHoldComplete:
                    transition(MachineOperationState::Hold);
                    hold_complete_ = true;
                    return true;
                case MachineEvent::GrblJog:         return transition(MachineOperationState::Jog);
                case MachineEvent::GrblDoor:
                case MachineEvent::GrblToolChange:  return transition(MachineOperationState::Hold);
                case MachineEvent::GrblSleep:       return transition(MachineOperationState::Idle);
                case MachineEvent::SyncTimeout:
                case MachineEvent::TeensyDisconnected:
                    return transition(MachineOperationState::TeensyDisconnected);
                case MachineEvent::JobLoaded:
                    if (job_loaded_) return false;
                    job_loaded_ = true; return true;
                case MachineEvent::JobUnloaded:
                    if (!job_loaded_) return false;
                    job_loaded_ = false; return true;
                default: return false;
            }

        // â”€â”€ TEENSY_DISCONNECTED â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::TeensyDisconnected:
            switch (ev) {
                case MachineEvent::TeensyConnected:
                    teensy_connected_ = true;
                    return transition(MachineOperationState::Syncing);
                case MachineEvent::FileUploadCmd:
                    if (!sd_mounted_) return false;
                    upload_active_ = true;
                    return transition(MachineOperationState::Uploading);
                case MachineEvent::JobLoaded:
                    if (job_loaded_) return false;
                    job_loaded_ = true; return true;
                case MachineEvent::JobUnloaded:
                    if (!job_loaded_) return false;
                    job_loaded_ = false; return true;
                default: return false;
            }

        // â”€â”€ IDLE â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Idle:
            switch (ev) {
                case MachineEvent::HomeCmd:
                    // Caller sends @HOME to Teensy; we wait for GrblHoming
                    return transition(MachineOperationState::Homing);
                case MachineEvent::JogCmd:
                    return transition(MachineOperationState::Jog);
                case MachineEvent::StartCmd:
                    if (!job_loaded_ || !teensy_connected_ || !all_axes_homed_) return false;
                    job_session_active_ = true;
                    return transition(MachineOperationState::Starting);
                case MachineEvent::FileUploadCmd:
                    if (!sd_mounted_) return false;
                    upload_active_ = true;
                    return transition(MachineOperationState::Uploading);
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::TeensyDisconnected);
                case MachineEvent::GrblAlarm:
                    return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::JobLoaded:
                    if (job_loaded_) return false;
                    job_loaded_ = true; return true;
                case MachineEvent::JobUnloaded:
                    if (!job_loaded_) return false;
                    job_loaded_ = false; return true;
                default: return false;
            }

        // â”€â”€ HOMING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Homing:
            switch (ev) {
                case MachineEvent::GrblIdle:
                    all_axes_homed_ = true;
                    return transition(MachineOperationState::Idle);
                case MachineEvent::GrblAlarm:
                    all_axes_homed_ = false;
                    return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::AbortCmd:
                    // Caller sends @RT_RESET to Teensy; wait for GrblIdle
                    return false;
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::CommsFault);
                default: return false;
            }

        // â”€â”€ JOG â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Jog:
            switch (ev) {
                case MachineEvent::GrblIdle:
                    return transition(MachineOperationState::Idle);
                case MachineEvent::GrblAlarm:
                    return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::JogStop:
                case MachineEvent::AbortCmd:
                    // Caller sends @JOG_CANCEL; wait for GrblIdle
                    return false;
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::CommsFault);
                default: return false;
            }

        // â”€â”€ STARTING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Starting:
            switch (ev) {
                case MachineEvent::GrblCycle:
                    return transition(MachineOperationState::Running);
                case MachineEvent::GrblIdle:
                    return handle_grbl_idle();
                case MachineEvent::GrblAlarm:
                    return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::AbortCmd:
                    if (abort_pending_) return false;
                    abort_pending_ = true;
                    return true; // caller sends @RT_RESET
                case MachineEvent::GcodeAck:
                case MachineEvent::GcodeError:
                    return false; // handled by streaming engine
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::CommsFault);
                default: return false;
            }

        // â”€â”€ RUNNING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Running:
            switch (ev) {
                case MachineEvent::GrblIdle:
                    return handle_grbl_idle();
                case MachineEvent::GrblCycle:
                    return false; // already running
                case MachineEvent::GrblHoldPending:
                    return transition(MachineOperationState::Hold);
                case MachineEvent::GrblAlarm:
                    return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::PauseCmd:
                    return false; // caller sends @RT_FEED_HOLD; wait for GrblHold
                case MachineEvent::AbortCmd:
                    if (abort_pending_) return false;
                    abort_pending_ = true;
                    return true; // caller sends @RT_RESET
                case MachineEvent::JobStreamComplete:
                    job_stream_complete_ = true;
                    return false;
                case MachineEvent::GcodeAck:
                case MachineEvent::GcodeError:
                    return false;
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::CommsFault);
                default: return false;
            }

        // â”€â”€ HOLD â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Hold:
            switch (ev) {
                case MachineEvent::GrblHoldComplete:
                    if (hold_complete_) return false;
                    hold_complete_ = true;
                    return true;
                case MachineEvent::GrblIdle:
                    return handle_grbl_idle();
                case MachineEvent::GrblCycle:
                    // Resume confirmed by GRBL
                    return transition(MachineOperationState::Running);
                case MachineEvent::ResumeCmd:
                    return false; // caller sends @RT_CYCLE_START; wait for GrblCycle
                case MachineEvent::AbortCmd:
                    if (abort_pending_) return false;
                    abort_pending_ = true;
                    return true; // caller sends @RT_RESET
                case MachineEvent::GrblAlarm:
                    return transition(MachineOperationState::Fault);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::CommsFault);
                default: return false;
            }

        // â”€â”€ FAULT â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Fault:
            switch (ev) {
                case MachineEvent::ResetCmd:
                    return false; // caller sends $X to Teensy; wait for GrblIdle
                case MachineEvent::GrblIdle:
                    return transition(MachineOperationState::Idle);
                case MachineEvent::GrblEstop:
                    return transition(MachineOperationState::Estop);
                case MachineEvent::TeensyDisconnected:
                    teensy_connected_ = false;
                    return transition(MachineOperationState::TeensyDisconnected);
                case MachineEvent::JobLoaded:
                    if (job_loaded_) return false;
                    job_loaded_ = true; return true;
                case MachineEvent::JobUnloaded:
                    if (!job_loaded_) return false;
                    job_loaded_ = false; return true;
                default: return false;
            }

        // â”€â”€ COMMS_FAULT â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::CommsFault:
            switch (ev) {
                case MachineEvent::TeensyConnected:
                    teensy_connected_ = true;
                    return transition(MachineOperationState::Syncing);
                default: return false;
            }

        // â”€â”€ ESTOP â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Estop:
            switch (ev) {
                case MachineEvent::ResetCmd:
                    if (hw_estop_active_) return false; // must release HW pin first
                    return false; // caller sends @RT_RESET; wait for GrblIdle
                case MachineEvent::GrblIdle:
                    if (hw_estop_active_) return false;
                    return transition(MachineOperationState::Idle);
                case MachineEvent::JobLoaded:
                    if (job_loaded_) return false;
                    job_loaded_ = true; return true;
                case MachineEvent::JobUnloaded:
                    if (!job_loaded_) return false;
                    job_loaded_ = false; return true;
                default: return false;
            }

        // â”€â”€ UPLOADING â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        case MachineOperationState::Uploading:
            switch (ev) {
                case MachineEvent::UploadComplete:
                case MachineEvent::UploadFailed:
                case MachineEvent::UploadAborted:
                    upload_active_ = false;
                    if (!teensy_connected_)
                        return transition(MachineOperationState::TeensyDisconnected);
                    return transition(MachineOperationState::Idle);
                case MachineEvent::SdRemoved:
                    // SD removed during upload â€” forced abort
                    upload_active_ = false;
                    sd_mounted_    = false;
                    job_loaded_  = false;
                    if (!teensy_connected_)
                        return transition(MachineOperationState::TeensyDisconnected);
                    return transition(MachineOperationState::Idle);
                default: return false;
            }

        default:
            return false;
    }
}
