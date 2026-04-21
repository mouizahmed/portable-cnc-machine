#pragma once

#include "app/machine/machine_fsm.h"
#include "app/operations/operation_request.h"
#include "app/storage/storage_service.h"
#include "app/storage/storage_transfer_fsm.h"
#include "app/worker/core1_worker_types.h"

enum class JobStreamState : uint8_t {
    Idle,
    Preparing,
    Beginning,
    Streaming,
    PausedByHold,
    Cancelling,
    Complete,
    Faulted,
};

class OperationCoordinator {
public:
    RequestDecision decide(const OperationRequest& request,
                           const MachineFsm& machine,
                           const StorageTransferStateMachine& storage,
                           JobStreamState stream,
                           const Core1WorkerSnapshot& worker,
                           StorageState storage_state) const;

private:
    static bool stream_active(JobStreamState stream);
    static bool machine_allows_storage(MachineOperationState state);
};
