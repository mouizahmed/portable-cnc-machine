#pragma once

#include <cstdint>

#include "app/job/job_state_machine.h"
#include "app/worker/core1_worker.h"
#include "core/state_types.h"
#include "drivers/sd_spi_card.h"

class StorageService {
public:
    StorageService(SdSpiCard& sd_card, Core1Worker& worker);

    void initialize(JobStateMachine& jobs);
    bool poll(JobStateMachine& jobs);
    bool force_remount(JobStateMachine& jobs);
    bool is_mounted() const;
    StorageState state() const;
    const char* status_text() const;
    bool refresh_job_files(JobStateMachine& jobs);
    uint64_t free_bytes() const;
    void set_cached_free_bytes(uint64_t free_bytes);
    bool begin_worker_health_check();
    bool apply_worker_health_result(bool healthy, JobStateMachine& jobs);

private:
    SdSpiCard& sd_card_;
    Core1Worker& worker_;
    StorageState state_ = StorageState::Uninitialized;
    uint32_t last_attempt_ms_ = 0;
    uint32_t last_health_check_ms_ = 0;
    uint8_t health_failure_count_ = 0;
    uint64_t cached_free_bytes_ = 0;
    bool worker_health_check_pending_ = false;

    bool try_mount(JobStateMachine& jobs);
};
