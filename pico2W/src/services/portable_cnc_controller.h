#pragma once

#include <cstdint>

#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_state_machine.h"
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
    PortableCncController(MachineStateMachine& machine,
                          JogStateMachine& jog,
                          JobStateMachine& jobs,
                          StorageService& storage);

    bool begin_calibration();
    bool complete_calibration();
    bool poll_storage();
    bool refresh_job_files();

    bool select_file(int16_t index);
    bool apply_control(ControlCommand command);
    bool apply_jog_action(JogAction action);
    bool handle_primary_action();

    MachineState machine_state() const;
    const JogStateMachine& jog() const;
    const JobStateMachine& jobs() const;
    StorageState storage_state() const;
    const char* storage_status_text() const;

    bool can_jog() const;
    bool can_select_file() const;
    bool can_run_selected_file() const;
    PrimaryAction primary_action() const;
    const char* primary_action_label() const;

private:
    MachineStateMachine& machine_;
    JogStateMachine& jog_;
    JobStateMachine& jobs_;
    StorageService& storage_;
};
