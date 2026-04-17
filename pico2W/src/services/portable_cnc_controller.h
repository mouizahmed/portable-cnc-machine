#pragma once

#include <cstdint>

#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_fsm.h"
#include "app/storage/storage_service.h"

enum class ControlCommand : uint8_t {
    Start,
    Pause,
    Resume,
};

enum class PrimaryAction : uint8_t {
    None,
    LoadJob,
    Start,
    Pause,
    Resume,
};

class PortableCncController {
public:
    PortableCncController(MachineFsm& machine,
                          JogStateMachine& jog,
                          JobStateMachine& jobs,
                          StorageService& storage);

    bool begin_calibration();
    bool complete_calibration();
    bool poll_storage();
    bool refresh_job_files();
    bool force_storage_remount();

    bool apply_control(ControlCommand command);
    bool apply_jog_action(JogAction action);
    bool handle_primary_action();

    MachineOperationState machine_state() const;
    const JogStateMachine& jog() const;
    const JobStateMachine& jobs() const;
    StorageState storage_state() const;
    const char* storage_status_text() const;
    uint64_t storage_free_bytes() const;

    bool can_jog() const;
    bool can_load_file() const;
    bool can_run_loaded_job() const;
    PrimaryAction primary_action() const;
    const char* primary_action_label() const;

private:
    MachineFsm& machine_;
    JogStateMachine& jog_;
    JobStateMachine& jobs_;
    StorageService& storage_;
};
