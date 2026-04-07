#pragma once

#include <cstdint>

#include "app/job/job_state_machine.h"
#include "core/state_types.h"
#include "drivers/sd_spi_card.h"

class StorageService {
public:
    explicit StorageService(SdSpiCard& sd_card);

    void initialize(JobStateMachine& jobs);
    bool poll(JobStateMachine& jobs);
    bool is_mounted() const;
    StorageState state() const;
    const char* status_text() const;
    bool refresh_job_files(JobStateMachine& jobs);

private:
    SdSpiCard& sd_card_;
    StorageState state_ = StorageState::Uninitialized;
    uint32_t last_attempt_ms_ = 0;
    uint32_t last_health_check_ms_ = 0;
    uint8_t health_failure_count_ = 0;

    bool try_mount(JobStateMachine& jobs);
    bool check_health(JobStateMachine& jobs);
};
