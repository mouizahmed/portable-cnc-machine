#pragma once
#include "core/state_types.h"

class MachineFsm {
public:
    MachineFsm();

    // Inject an event. Returns true if a state or flag change occurred
    // (caller should re-emit @STATE / @CAPS / @SAFETY).
    bool handle_event(MachineEvent event);

    MachineOperationState state() const { return state_; }
    SafetyLevel           safety() const;
    CapsFlags             caps() const;

    // Individual flag accessors (for @CAPS / @INFO responses)
    bool teensy_connected()   const { return teensy_connected_; }
    bool sd_mounted()         const { return sd_mounted_; }
    bool job_loaded()       const { return job_loaded_; }
    bool all_axes_homed()     const { return all_axes_homed_; }
    bool hold_complete()      const { return hold_complete_; }
    bool hw_estop_active()    const { return hw_estop_active_; }
    bool job_session_active() const { return job_session_active_; }
    bool upload_active()      const { return upload_active_; }

    // Called by the streaming engine when it has completed sending all lines
    // and receiving all oks. Equivalent to injecting JobStreamComplete.
    void notify_stream_complete();

private:
    MachineOperationState state_ = MachineOperationState::Booting;

    // Internal flags per STATE_MACHINE.md
    bool teensy_connected_    = false;
    bool job_loaded_        = false;
    bool job_session_active_  = false;
    bool job_stream_complete_ = false;
    bool abort_pending_       = false;
    bool hold_complete_       = false;
    bool all_axes_homed_      = false;
    bool sd_mounted_          = false;
    bool upload_active_       = false;
    bool hw_estop_active_     = false;

    // Transition helpers â€” return true if state actually changed
    bool transition(MachineOperationState next);

    // Per-state entry cleanup (clears stale flags per STATE_MACHINE.md)
    void on_enter(MachineOperationState s);

    // Handle GrblIdle with full context logic (job session / abort / complete)
    bool handle_grbl_idle();
};
