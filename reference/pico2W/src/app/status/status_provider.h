#pragma once

#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_fsm.h"
#include "app/storage/storage_service.h"
#include "protocol/usb_cdc_transport.h"
#include "ui/components/ui_shell_types.h"

class StatusProvider {
public:
    StatusProvider(const MachineFsm& machine_fsm,
                   const JogStateMachine& jog_state_machine,
                   const JobStateMachine& job_state_machine,
                   const StorageService& storage_service,
                   const UsbCdcTransport& usb_transport);

    StatusSnapshot current() const;

private:
    const MachineFsm& machine_fsm_;
    const JogStateMachine& jog_state_machine_;
    const JobStateMachine& job_state_machine_;
    const StorageService& storage_service_;
    const UsbCdcTransport& usb_transport_;
    mutable char xyz_text_[24]{};
};
