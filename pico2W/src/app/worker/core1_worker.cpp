#include "app/worker/core1_worker.h"

#include <cstdio>
#include <cstring>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "drivers/sd_spi_card.h"

namespace {
void copy_text(char* dest, std::size_t size, const char* src) {
    if (dest == nullptr || size == 0) {
        return;
    }
    dest[0] = '\0';
    if (src == nullptr) {
        return;
    }
    std::strncpy(dest, src, size - 1u);
    dest[size - 1u] = '\0';
}

void make_storage_path(const char* filename, char* path, std::size_t path_size) {
    if (path == nullptr || path_size == 0) {
        return;
    }
    std::snprintf(path, path_size, "0:/%s", filename != nullptr ? filename : "");
}

bool has_extension(const char* name, const char* extension) {
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }

    while (*dot != '\0' && *extension != '\0') {
        char lhs = *dot++;
        char rhs = *extension++;
        if (lhs >= 'a' && lhs <= 'z') {
            lhs = static_cast<char>(lhs - ('a' - 'A'));
        }
        if (rhs >= 'a' && rhs <= 'z') {
            rhs = static_cast<char>(rhs - ('a' - 'A'));
        }
        if (lhs != rhs) {
            return false;
        }
    }

    return *dot == '\0' && *extension == '\0';
}

Core1JobIntent default_intent_for_job(Core1JobType type) {
    switch (type) {
        case Core1JobType::StorageAbortTransfer:
            return Core1JobIntent::Urgent;

        case Core1JobType::StorageOpenUpload:
        case Core1JobType::StorageWriteUploadChunk:
        case Core1JobType::StorageFinalizeUpload:
        case Core1JobType::StorageDeleteFile:
            return Core1JobIntent::ForegroundExclusive;

        case Core1JobType::StorageOpenDownload:
        case Core1JobType::StorageReadDownloadChunk:
        case Core1JobType::StorageCloseDownload:
        case Core1JobType::StorageListBegin:
        case Core1JobType::StorageListNextPage:
            return Core1JobIntent::ForegroundPreemptible;

        case Core1JobType::StorageFreeSpace:
        case Core1JobType::StorageHealthCheck:
            return Core1JobIntent::BackgroundDisposable;

        case Core1JobType::None:
        default:
            return Core1JobIntent::Unspecified;
    }
}

Core1JobIntent effective_intent(const Core1Job& job) {
    return job.intent == Core1JobIntent::Unspecified
        ? default_intent_for_job(job.type)
        : job.intent;
}

Core1JobIntent effective_result_intent(const Core1Result& result) {
    return default_intent_for_job(result.source_job);
}

bool intent_is_foreground(Core1JobIntent intent) {
    return intent == Core1JobIntent::Urgent ||
           intent == Core1JobIntent::ForegroundExclusive ||
           intent == Core1JobIntent::ForegroundPreemptible;
}

bool intent_is_background(Core1JobIntent intent) {
    return intent == Core1JobIntent::BackgroundDisposable;
}

bool intent_is_exclusive_sd(Core1JobIntent intent) {
    return intent == Core1JobIntent::ForegroundExclusive;
}

bool intent_is_preemptible_foreground(Core1JobIntent intent) {
    return intent == Core1JobIntent::ForegroundPreemptible;
}

void accumulate_job_intent(const Core1Job& job,
                           bool& has_foreground,
                           bool& has_background,
                           bool& has_exclusive_sd,
                           bool& has_preemptible_foreground,
                           bool& urgent_pending) {
    const Core1JobIntent intent = effective_intent(job);
    has_foreground = has_foreground || intent_is_foreground(intent);
    has_background = has_background || intent_is_background(intent);
    has_exclusive_sd = has_exclusive_sd || intent_is_exclusive_sd(intent);
    has_preemptible_foreground = has_preemptible_foreground ||
                                 intent_is_preemptible_foreground(intent);
    urgent_pending = urgent_pending || intent == Core1JobIntent::Urgent;
}

void accumulate_result_intent(const Core1Result& result,
                              bool& has_foreground,
                              bool& has_background,
                              bool& has_exclusive_sd,
                              bool& has_preemptible_foreground,
                              bool& urgent_pending) {
    const Core1JobIntent intent = effective_result_intent(result);
    has_foreground = has_foreground || intent_is_foreground(intent);
    has_background = has_background || intent_is_background(intent);
    has_exclusive_sd = has_exclusive_sd || intent_is_exclusive_sd(intent);
    has_preemptible_foreground = has_preemptible_foreground ||
                                 intent_is_preemptible_foreground(intent);
    urgent_pending = urgent_pending || intent == Core1JobIntent::Urgent;
}
}  // namespace

Core1Worker* Core1Worker::instance_ = nullptr;
alignas(8) uint32_t Core1Worker::worker_stack_[Core1Worker::kWorkerStackBytes / sizeof(uint32_t)]{};

Core1Worker::Core1Worker() {
    critical_section_init(&lock_);
    lock_initialized_ = true;
    urgent_queue_.capacity = kUrgentQueueCapacity;
    control_queue_.capacity = kControlQueueCapacity;
    bulk_queue_.capacity = kBulkQueueCapacity;
}

bool Core1Worker::start() {
    if (started_) {
        return true;
    }

    if (instance_ != nullptr && instance_ != this) {
        return false;
    }

    instance_ = this;
    multicore_launch_core1_with_stack(&Core1Worker::entry,
                                      worker_stack_,
                                      sizeof(worker_stack_));

    const absolute_time_t deadline = make_timeout_time_ms(100);
    while (!lockout_ready_ && !time_reached(deadline)) {
        sleep_us(10);
    }

    return started_;
}

bool Core1Worker::submit_urgent(const Core1Job& job) {
    return submit(urgent_queue_, job);
}

bool Core1Worker::submit_control(const Core1Job& job) {
    return submit(control_queue_, job);
}

bool Core1Worker::submit_bulk(const Core1Job& job) {
    return submit(bulk_queue_, job);
}

bool Core1Worker::try_pop_result(Core1Result& result) {
    critical_section_enter_blocking(&lock_);
    if (result_queue_.count == 0) {
        critical_section_exit(&lock_);
        return false;
    }

    result = result_queue_.entries[result_queue_.head];
    result_queue_.head = (result_queue_.head + 1u) % kResultQueueCapacity;
    --result_queue_.count;
    critical_section_exit(&lock_);
    return true;
}

Core1WorkerSnapshot Core1Worker::snapshot() const {
    Core1WorkerSnapshot snap{};
    critical_section_enter_blocking(&lock_);
    snap.started = started_;
    snap.lockout_ready = lockout_ready_;
    snap.busy = busy_;
    snap.active_job = active_job_;
    snap.active_operation = active_job_;
    snap.active_intent = busy_ ? effective_intent(current_job_) : Core1JobIntent::Unspecified;
    snap.heartbeat_counter = heartbeat_counter_;
    snap.urgent_queue_count = urgent_queue_.count;
    snap.control_queue_count = control_queue_.count;
    snap.bulk_queue_count = bulk_queue_.count;
    snap.result_queue_count = result_queue_.count;
    snap.urgent_queue_high_water = urgent_queue_.high_water;
    snap.control_queue_high_water = control_queue_.high_water;
    snap.bulk_queue_high_water = bulk_queue_.high_water;
    snap.result_queue_high_water = result_queue_.high_water;

    bool has_foreground = false;
    bool has_background = false;
    bool has_exclusive_sd = false;
    bool has_preemptible_foreground = false;
    bool urgent_pending = false;

    if (busy_) {
        accumulate_job_intent(current_job_,
                              has_foreground,
                              has_background,
                              has_exclusive_sd,
                              has_preemptible_foreground,
                              urgent_pending);
    }

    const JobQueue* queues[] = {&urgent_queue_, &control_queue_, &bulk_queue_};
    for (const JobQueue* queue : queues) {
        std::size_t index = queue->head;
        for (std::size_t count = 0; count < queue->count; ++count) {
            accumulate_job_intent(queue->entries[index],
                                  has_foreground,
                                  has_background,
                                  has_exclusive_sd,
                                  has_preemptible_foreground,
                                  urgent_pending);
            index = (index + 1u) % queue->capacity;
        }
    }

    std::size_t result_index = result_queue_.head;
    for (std::size_t count = 0; count < result_queue_.count; ++count) {
        accumulate_result_intent(result_queue_.entries[result_index],
                                 has_foreground,
                                 has_background,
                                 has_exclusive_sd,
                                 has_preemptible_foreground,
                                 urgent_pending);
        result_index = (result_index + 1u) % kResultQueueCapacity;
    }

    snap.has_foreground_work = has_foreground;
    snap.has_background_only_work = has_background && !has_foreground;
    snap.has_exclusive_sd_work = has_exclusive_sd;
    snap.has_preemptible_foreground_work = has_preemptible_foreground;
    snap.urgent_pending = urgent_pending;
    critical_section_exit(&lock_);
    return snap;
}

bool Core1Worker::idle() const {
    const Core1WorkerSnapshot snap = snapshot();
    return !snap.busy &&
           snap.urgent_queue_count == 0 &&
           snap.control_queue_count == 0 &&
           snap.bulk_queue_count == 0 &&
           snap.result_queue_count == 0;
}

void Core1Worker::clear_pending_jobs() {
    critical_section_enter_blocking(&lock_);
    urgent_queue_.head = urgent_queue_.tail = urgent_queue_.count = urgent_queue_.high_water = 0;
    control_queue_.head = control_queue_.tail = control_queue_.count = control_queue_.high_water = 0;
    bulk_queue_.head = bulk_queue_.tail = bulk_queue_.count = bulk_queue_.high_water = 0;
    critical_section_exit(&lock_);
}

void Core1Worker::clear_results() {
    critical_section_enter_blocking(&lock_);
    result_queue_.head = result_queue_.tail = result_queue_.count = result_queue_.high_water = 0;
    critical_section_exit(&lock_);
}

void Core1Worker::entry() {
    if (instance_ != nullptr) {
        instance_->loop();
    }
}

void Core1Worker::loop() {
    multicore_lockout_victim_init();

    critical_section_enter_blocking(&lock_);
    lockout_ready_ = true;
    started_ = true;
    critical_section_exit(&lock_);

    while (true) {
        ++heartbeat_counter_;

        Core1Job job{};
        if (!pop_next_job(job)) {
            sleep_us(100);
            continue;
        }

        execute_job(job);

        critical_section_enter_blocking(&lock_);
        current_job_ = Core1Job{};
        active_job_ = Core1JobType::None;
        busy_ = false;
        critical_section_exit(&lock_);
    }
}

bool Core1Worker::submit(JobQueue& queue, const Core1Job& job) {
    if (job.type == Core1JobType::None) {
        return false;
    }

    critical_section_enter_blocking(&lock_);
    if (queue.count >= queue.capacity) {
        critical_section_exit(&lock_);
        return false;
    }

    queue.entries[queue.tail] = job;
    queue.tail = (queue.tail + 1u) % queue.capacity;
    ++queue.count;
    if (queue.count > queue.high_water) {
        queue.high_water = queue.count;
    }
    critical_section_exit(&lock_);
    return true;
}

bool Core1Worker::pop_next_job(Core1Job& job) {
    critical_section_enter_blocking(&lock_);
    const bool popped = pop_job(urgent_queue_, job) ||
                        pop_job(control_queue_, job) ||
                        pop_job(bulk_queue_, job);
    if (popped) {
        busy_ = true;
        active_job_ = job.type;
        current_job_ = job;
    }
    critical_section_exit(&lock_);
    return popped;
}

bool Core1Worker::pop_job(JobQueue& queue, Core1Job& job) {
    if (queue.count == 0) {
        return false;
    }

    job = queue.entries[queue.head];
    queue.head = (queue.head + 1u) % queue.capacity;
    --queue.count;
    return true;
}

void Core1Worker::push_result(const Core1Result& result) {
    critical_section_enter_blocking(&lock_);
    if (result_queue_.count < kResultQueueCapacity) {
        result_queue_.entries[result_queue_.tail] = result;
        result_queue_.tail = (result_queue_.tail + 1u) % kResultQueueCapacity;
        ++result_queue_.count;
        if (result_queue_.count > result_queue_.high_water) {
            result_queue_.high_water = result_queue_.count;
        }
    }
    critical_section_exit(&lock_);
}

void Core1Worker::execute_job(const Core1Job& job) {
    switch (job.type) {
        case Core1JobType::StorageOpenUpload:
            execute_open_upload(job);
            return;

        case Core1JobType::StorageWriteUploadChunk:
            execute_write_upload_chunk(job);
            return;

        case Core1JobType::StorageFinalizeUpload:
            execute_finalize_upload(job);
            return;

        case Core1JobType::StorageOpenDownload:
            execute_open_download(job);
            return;

        case Core1JobType::StorageReadDownloadChunk:
            execute_read_download_chunk(job);
            return;

        case Core1JobType::StorageCloseDownload:
            execute_close_download(job);
            return;

        case Core1JobType::StorageListBegin:
            execute_list_begin(job);
            return;

        case Core1JobType::StorageListNextPage:
            execute_list_next_page(job);
            return;

        case Core1JobType::StorageDeleteFile:
            execute_delete_file(job);
            return;

        case Core1JobType::StorageFreeSpace:
            execute_free_space(job);
            return;

        case Core1JobType::StorageHealthCheck:
            execute_health_check(job);
            return;

        case Core1JobType::StorageAbortTransfer:
            execute_abort_transfer(job);
            return;

        case Core1JobType::None:
        default: {
            Core1Result result{};
            result.type = Core1ResultType::WorkerFault;
            result.source_job = job.type;
            push_result(result);
            return;
        }
    }
}

void Core1Worker::execute_open_upload(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::FileOpened;
    result.source_job = job.type;
    result.open_upload.transfer_id = job.open_upload.transfer_id;
    result.open_upload.expected_size = job.open_upload.expected_size;
    result.open_upload.overwrite = job.open_upload.overwrite;
    copy_text(result.open_upload.filename,
              sizeof(result.open_upload.filename),
              job.open_upload.filename);

    FRESULT close_result = FR_OK;
    close_upload_file(close_result);

    char path[80]{};
    make_storage_path(job.open_upload.filename, path, sizeof(path));

    FILINFO info{};
    result.open_upload.stat_result = f_stat(path, &info);
    result.open_upload.file_existed = result.open_upload.stat_result == FR_OK;
    if (result.open_upload.file_existed && !job.open_upload.overwrite) {
        result.open_upload.open_result = FR_EXIST;
        push_result(result);
        return;
    }

    if (result.open_upload.file_existed && job.open_upload.overwrite) {
        f_unlink(path);
    }

    FATFS* fs = nullptr;
    DWORD free_clust = 0;
    if (f_getfree("0:", &free_clust, &fs) == FR_OK && fs != nullptr) {
        result.open_upload.free_bytes =
            static_cast<uint64_t>(free_clust) * fs->csize * 512u;
    }
    if (job.open_upload.expected_size > 0 &&
        static_cast<uint64_t>(job.open_upload.expected_size) > result.open_upload.free_bytes) {
        result.open_upload.open_result = FR_DENIED;
        push_result(result);
        return;
    }

    upload_file_ = FIL{};
    result.open_upload.open_result = f_open(&upload_file_, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (result.open_upload.open_result != FR_OK) {
        push_result(result);
        return;
    }

    upload_file_open_ = true;
    upload_transfer_id_ = job.open_upload.transfer_id;
    copy_text(upload_filename_, sizeof(upload_filename_), job.open_upload.filename);

    if (job.open_upload.expected_size > 0) {
        const uint32_t prealloc_start_ms = to_ms_since_boot(get_absolute_time());
        result.open_upload.prealloc_result = f_expand(&upload_file_, job.open_upload.expected_size, 1);
        result.open_upload.prealloc_elapsed_ms =
            to_ms_since_boot(get_absolute_time()) - prealloc_start_ms;
        if (result.open_upload.prealloc_result != FR_OK) {
            FRESULT ignored = FR_OK;
            close_upload_file(ignored);
            f_unlink(path);
        }
    }

    push_result(result);
}

void Core1Worker::execute_write_upload_chunk(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::UploadChunkCommitted;
    result.source_job = job.type;
    result.upload.transfer_id = job.upload.transfer_id;
    result.upload.sequence = job.upload.sequence;
    result.upload.length = job.upload.length;
    result.upload.crc_after = job.upload.crc_after;

    if (!upload_file_open_ ||
        job.upload.transfer_id != upload_transfer_id_ ||
        job.upload.length == 0 ||
        job.upload.length > kCore1WorkerTransferChunkBytes) {
        result.upload.result = FR_INVALID_PARAMETER;
        result.upload.written = 0;
        push_result(result);
        return;
    }

    const uint32_t write_start_ms = to_ms_since_boot(get_absolute_time());
    result.upload.result = f_write(&upload_file_,
                                   job.upload.payload,
                                   job.upload.length,
                                   &result.upload.written);
    result.upload.write_elapsed_ms =
        to_ms_since_boot(get_absolute_time()) - write_start_ms;
    push_result(result);
}

void Core1Worker::execute_finalize_upload(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::FileClosed;
    result.source_job = job.type;
    result.finalize_upload.transfer_id = job.finalize_upload.transfer_id;

    if (!upload_file_open_ || job.finalize_upload.transfer_id != upload_transfer_id_) {
        result.finalize_upload.result = FR_INVALID_PARAMETER;
        push_result(result);
        return;
    }

    char filename[sizeof(upload_filename_)]{};
    copy_text(filename, sizeof(filename), upload_filename_);

    const uint32_t close_start_ms = to_ms_since_boot(get_absolute_time());
    close_upload_file(result.finalize_upload.result);
    result.finalize_upload.close_elapsed_ms =
        to_ms_since_boot(get_absolute_time()) - close_start_ms;

    if (job.finalize_upload.delete_on_error && result.finalize_upload.result != FR_OK) {
        char path[80]{};
        make_storage_path(filename, path, sizeof(path));
        f_unlink(path);
    }
    push_result(result);
}

void Core1Worker::execute_abort_transfer(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::TransferAborted;
    result.source_job = job.type;
    result.abort_transfer.transfer_id = job.abort_transfer.transfer_id;
    result.abort_transfer.deleted_partial = job.abort_transfer.delete_partial;

    if (upload_file_open_ && job.abort_transfer.transfer_id == upload_transfer_id_) {
        close_upload_file(result.abort_transfer.close_result);
    }
    if (download_file_open_ && job.abort_transfer.transfer_id == download_transfer_id_) {
        close_download_file(result.abort_transfer.close_result);
    }
    close_list_dir();

    if (job.abort_transfer.delete_partial) {
        char path[80]{};
        const char* filename = job.abort_transfer.filename[0] != '\0'
            ? job.abort_transfer.filename
            : upload_filename_;
        make_storage_path(filename, path, sizeof(path));
        result.abort_transfer.unlink_result = f_unlink(path);
    }

    push_result(result);
}

void Core1Worker::execute_list_begin(const Core1Job& job) {
    (void)job;
    close_list_dir();
    list_total_count_ = 0;

    Core1Result result{};
    result.type = Core1ResultType::FileListPage;
    result.source_job = Core1JobType::StorageListBegin;
    result.file_list.result = f_opendir(&list_dir_, "0:/");
    if (result.file_list.result != FR_OK) {
        result.file_list.complete = true;
        push_result(result);
        return;
    }

    list_dir_open_ = true;
    fill_list_page(result);
    push_result(result);
}

void Core1Worker::execute_list_next_page(const Core1Job& job) {
    (void)job;
    Core1Result result{};
    result.type = Core1ResultType::FileListPage;
    result.source_job = Core1JobType::StorageListNextPage;

    if (!list_dir_open_) {
        result.file_list.result = FR_INVALID_OBJECT;
        result.file_list.complete = true;
        push_result(result);
        return;
    }

    fill_list_page(result);
    push_result(result);
}

void Core1Worker::execute_delete_file(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::FileDeleted;
    result.source_job = job.type;
    copy_text(result.delete_file.filename,
              sizeof(result.delete_file.filename),
              job.delete_file.filename);

    char path[80]{};
    make_storage_path(job.delete_file.filename, path, sizeof(path));
    result.delete_file.result = f_unlink(path);
    push_result(result);
}

void Core1Worker::execute_free_space(const Core1Job& job) {
    (void)job;
    Core1Result result{};
    result.type = Core1ResultType::FreeSpaceReady;
    result.source_job = Core1JobType::StorageFreeSpace;
    query_free_space(result.free_space);
    push_result(result);
}

void Core1Worker::execute_health_check(const Core1Job& job) {
    (void)job;
    Core1Result result{};
    result.type = Core1ResultType::StorageHealthReady;
    result.source_job = Core1JobType::StorageHealthCheck;
    uint8_t sector_buffer[512]{};
    SdSpiCard* card = sd_spi_card_active();
    result.health.healthy = card != nullptr && card->read_blocks(0, sector_buffer, 1);
    push_result(result);
}

void Core1Worker::execute_open_download(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::DownloadOpened;
    result.source_job = job.type;
    result.open_download.transfer_id = job.open_download.transfer_id;
    copy_text(result.open_download.filename,
              sizeof(result.open_download.filename),
              job.open_download.filename);

    FRESULT close_result = FR_OK;
    close_download_file(close_result);

    char path[80]{};
    make_storage_path(job.open_download.filename, path, sizeof(path));

    FILINFO info{};
    result.open_download.stat_result = f_stat(path, &info);
    if (result.open_download.stat_result != FR_OK) {
        push_result(result);
        return;
    }

    download_file_ = FIL{};
    result.open_download.open_result = f_open(&download_file_, path, FA_READ);
    if (result.open_download.open_result == FR_OK) {
        result.open_download.size = info.fsize;
        download_file_open_ = true;
        download_transfer_id_ = job.open_download.transfer_id;
        copy_text(download_filename_, sizeof(download_filename_), job.open_download.filename);
    }

    push_result(result);
}

void Core1Worker::execute_read_download_chunk(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::DownloadChunkRead;
    result.source_job = job.type;
    result.read_download.transfer_id = job.read_download.transfer_id;
    result.read_download.sequence = job.read_download.sequence;

    if (!download_file_open_ || job.read_download.transfer_id != download_transfer_id_) {
        result.read_download.result = FR_INVALID_PARAMETER;
        push_result(result);
        return;
    }

    UINT bytes_read = 0;
    result.read_download.result = f_read(&download_file_,
                                         result.read_download.payload,
                                         sizeof(result.read_download.payload),
                                         &bytes_read);
    result.read_download.length = static_cast<uint16_t>(bytes_read);
    push_result(result);
}

void Core1Worker::execute_close_download(const Core1Job& job) {
    Core1Result result{};
    result.type = Core1ResultType::DownloadClosed;
    result.source_job = job.type;
    result.close_download.transfer_id = job.close_download.transfer_id;

    if (!download_file_open_ || job.close_download.transfer_id != download_transfer_id_) {
        result.close_download.result = FR_OK;
        push_result(result);
        return;
    }

    close_download_file(result.close_download.result);
    push_result(result);
}

void Core1Worker::close_upload_file(FRESULT& result) {
    result = FR_OK;
    if (!upload_file_open_) {
        return;
    }

    result = f_sync(&upload_file_);
    if (result == FR_OK) {
        result = f_close(&upload_file_);
    } else {
        f_close(&upload_file_);
    }
    upload_file_ = FIL{};
    upload_file_open_ = false;
    upload_transfer_id_ = 0;
    upload_filename_[0] = '\0';
}

void Core1Worker::close_download_file(FRESULT& result) {
    result = FR_OK;
    if (!download_file_open_) {
        return;
    }

    result = f_close(&download_file_);
    download_file_ = FIL{};
    download_file_open_ = false;
    download_transfer_id_ = 0;
    download_filename_[0] = '\0';
}

void Core1Worker::close_list_dir() {
    if (list_dir_open_) {
        f_closedir(&list_dir_);
    }
    list_dir_ = DIR{};
    list_dir_open_ = false;
    list_total_count_ = 0;
}

void Core1Worker::fill_list_page(Core1Result& result) {
    result.file_list.result = FR_OK;
    result.file_list.complete = false;
    result.file_list.entry_count = 0;

    while (result.file_list.entry_count < kCore1WorkerFileListPageEntries) {
        FILINFO info{};
        const FRESULT fr = f_readdir(&list_dir_, &info);
        if (fr != FR_OK) {
            result.file_list.result = fr;
            result.file_list.complete = true;
            close_list_dir();
            return;
        }

        if (info.fname[0] == '\0') {
            result.file_list.complete = true;
            result.file_list.total_count = list_total_count_;
            Core1FreeSpaceResult free_space{};
            query_free_space(free_space);
            result.file_list.free_bytes = free_space.free_bytes;
            close_list_dir();
            return;
        }

        if ((info.fattrib & AM_DIR) != 0 || !is_supported_job_file(info.fname)) {
            continue;
        }

        Core1FileListEntry& entry = result.file_list.entries[result.file_list.entry_count++];
        copy_text(entry.name, sizeof(entry.name), info.fname);
        entry.size = info.fsize;
        ++list_total_count_;
    }

    result.file_list.total_count = list_total_count_;
}

bool Core1Worker::is_supported_job_file(const char* name) {
    return has_extension(name, ".NC") ||
           has_extension(name, ".GCODE") ||
           has_extension(name, ".TAP") ||
           has_extension(name, ".NGC") ||
           has_extension(name, ".GC");
}

void Core1Worker::query_free_space(Core1FreeSpaceResult& result) {
    FATFS* fs = nullptr;
    DWORD free_clust = 0;
    result.result = f_getfree("0:", &free_clust, &fs);
    if (result.result == FR_OK && fs != nullptr) {
        result.free_bytes = static_cast<uint64_t>(free_clust) * fs->csize * 512u;
    }
}
