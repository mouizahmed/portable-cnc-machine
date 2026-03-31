#pragma once

#include "app/job/job_state_machine.h"
#include "app/machine/machine_state_machine.h"
#include "app/storage/storage_service.h"
#include "ui/components/ui_shell_types.h"

class StatusProvider {
public:
    StatusProvider(const MachineStateMachine& machine_state_machine,
                   const JobStateMachine& job_state_machine,
                   const StorageService& storage_service);

    StatusSnapshot current() const;

private:
    const MachineStateMachine& machine_state_machine_;
    const JobStateMachine& job_state_machine_;
    const StorageService& storage_service_;
};
