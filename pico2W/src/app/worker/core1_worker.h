#pragma once

#include "app/worker/core1_worker_types.h"
#include "pico/sync.h"

class Core1Worker {
public:
    Core1Worker();

    bool start();
    bool submit_urgent(const Core1Job& job);
    bool submit_control(const Core1Job& job);
    bool submit_bulk(const Core1Job& job);
    bool try_pop_result(Core1Result& result);
    Core1WorkerSnapshot snapshot() const;
    bool idle() const;

    void clear_background_jobs();
    void clear_pending_jobs();
    void clear_results();

private:
    static constexpr std::size_t kUrgentQueueCapacity = 4;
    static constexpr std::size_t kControlQueueCapacity = 4;
    static constexpr std::size_t kBulkQueueCapacity = 16;
    static constexpr std::size_t kResultQueueCapacity = 16;
    static constexpr std::size_t kWorkerStackBytes = 8192;

    struct JobQueue {
        Core1Job entries[kBulkQueueCapacity]{};
        std::size_t head = 0;
        std::size_t tail = 0;
        std::size_t count = 0;
        std::size_t high_water = 0;
        std::size_t capacity = 0;
    };

    struct ResultQueue {
        Core1Result entries[kResultQueueCapacity]{};
        std::size_t head = 0;
        std::size_t tail = 0;
        std::size_t count = 0;
        std::size_t high_water = 0;
    };

    mutable critical_section_t lock_{};
    bool lock_initialized_ = false;
    volatile bool started_ = false;
    volatile bool lockout_ready_ = false;
    volatile bool busy_ = false;
    volatile uint32_t heartbeat_counter_ = 0;
    Core1JobType active_job_ = Core1JobType::None;
    JobQueue urgent_queue_{};
    JobQueue control_queue_{};
    JobQueue bulk_queue_{};
    ResultQueue result_queue_{};
    Core1Job current_job_{};
    FIL upload_file_{};
    bool upload_file_open_ = false;
    uint8_t upload_transfer_id_ = 0;
    char upload_filename_[64]{};
    FIL download_file_{};
    bool download_file_open_ = false;
    uint8_t download_transfer_id_ = 0;
    char download_filename_[64]{};
    DIR list_dir_{};
    bool list_dir_open_ = false;
    uint32_t list_total_count_ = 0;

    static Core1Worker* instance_;
    static uint32_t worker_stack_[kWorkerStackBytes / sizeof(uint32_t)];

    static void entry();
    void loop();
    bool submit(JobQueue& queue, const Core1Job& job);
    bool pop_next_job(Core1Job& job);
    bool pop_job(JobQueue& queue, Core1Job& job);
    void push_result(const Core1Result& result);
    void execute_job(const Core1Job& job);
    void execute_open_upload(const Core1Job& job);
    void execute_write_upload_chunk(const Core1Job& job);
    void execute_finalize_upload(const Core1Job& job);
    void execute_open_download(const Core1Job& job);
    void execute_read_download_chunk(const Core1Job& job);
    void execute_close_download(const Core1Job& job);
    void execute_list_begin(const Core1Job& job);
    void execute_list_next_page(const Core1Job& job);
    void execute_delete_file(const Core1Job& job);
    void execute_free_space(const Core1Job& job);
    void execute_health_check(const Core1Job& job);
    void execute_abort_transfer(const Core1Job& job);
    void execute_stream_prepare_begin(const Core1Job& job);
    void execute_stream_prepare_next_batch(const Core1Job& job);
    void execute_stream_cancel(const Core1Job& job);
    void close_upload_file(FRESULT& result);
    void close_download_file(FRESULT& result);
    void close_stream_file();
    void close_list_dir();
    void fill_list_page(Core1Result& result);
    static bool is_supported_job_file(const char* name);
    static void query_free_space(Core1FreeSpaceResult& result);

    FIL stream_file_{};
    bool stream_file_open_ = false;
    int16_t stream_loaded_index_ = -1;
    char stream_filename_[64]{};
    uint32_t stream_total_lines_ = 0;
};
