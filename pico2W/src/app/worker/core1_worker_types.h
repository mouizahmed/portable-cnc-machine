#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include "ff.h"
}

constexpr std::size_t kCore1WorkerTransferChunkBytes = 4096;
constexpr std::size_t kCore1WorkerFileListPageEntries = 8;

enum class Core1JobIntent : uint8_t {
    Unspecified,
    Urgent,
    ForegroundExclusive,
    ForegroundPreemptible,
    BackgroundDisposable,
};

enum class Core1JobSource : uint8_t {
    Unknown,
    Desktop,
    Tft,
    StorageService,
    System,
};

enum class Core1JobType : uint8_t {
    None,
    StorageOpenUpload,
    StorageWriteUploadChunk,
    StorageFinalizeUpload,
    StorageOpenDownload,
    StorageReadDownloadChunk,
    StorageCloseDownload,
    StorageListBegin,
    StorageListNextPage,
    StorageDeleteFile,
    StorageFreeSpace,
    StorageHealthCheck,
    StorageAbortTransfer,
};

enum class Core1ResultType : uint8_t {
    None,
    FileOpened,
    UploadChunkCommitted,
    FileClosed,
    DownloadOpened,
    DownloadChunkRead,
    DownloadClosed,
    FileListPage,
    FileDeleted,
    FreeSpaceReady,
    StorageHealthReady,
    TransferAborted,
    StorageError,
    WorkerFault,
};

struct Core1UploadOpenJob {
    uint8_t transfer_id = 0;
    uint32_t expected_size = 0;
    bool overwrite = false;
    char filename[64]{};
};

struct Core1UploadChunkJob {
    uint8_t transfer_id = 0;
    uint32_t sequence = 0;
    uint16_t length = 0;
    uint32_t crc_after = 0xFFFFFFFFu;
    uint8_t payload[kCore1WorkerTransferChunkBytes]{};
};

struct Core1UploadFinalizeJob {
    uint8_t transfer_id = 0;
    bool delete_on_error = false;
};

struct Core1UploadAbortJob {
    uint8_t transfer_id = 0;
    bool delete_partial = false;
    char filename[64]{};
};

struct Core1DownloadOpenJob {
    uint8_t transfer_id = 0;
    char filename[64]{};
};

struct Core1DownloadReadJob {
    uint8_t transfer_id = 0;
    uint32_t sequence = 0;
};

struct Core1DownloadCloseJob {
    uint8_t transfer_id = 0;
};

struct Core1DeleteFileJob {
    char filename[64]{};
};

struct Core1Job {
    Core1JobType type = Core1JobType::None;
    Core1JobIntent intent = Core1JobIntent::Unspecified;
    Core1JobSource source = Core1JobSource::Unknown;
    Core1UploadOpenJob open_upload{};
    Core1UploadChunkJob upload{};
    Core1UploadFinalizeJob finalize_upload{};
    Core1DownloadOpenJob open_download{};
    Core1DownloadReadJob read_download{};
    Core1DownloadCloseJob close_download{};
    Core1DeleteFileJob delete_file{};
    Core1UploadAbortJob abort_transfer{};
};

struct Core1UploadOpenResult {
    uint8_t transfer_id = 0;
    uint32_t expected_size = 0;
    uint32_t prealloc_elapsed_ms = 0;
    uint64_t free_bytes = 0;
    FRESULT stat_result = FR_OK;
    FRESULT open_result = FR_OK;
    FRESULT prealloc_result = FR_OK;
    bool file_existed = false;
    bool overwrite = false;
    char filename[64]{};
};

struct Core1UploadChunkResult {
    uint8_t transfer_id = 0;
    uint32_t sequence = 0;
    uint16_t length = 0;
    uint32_t crc_after = 0xFFFFFFFFu;
    uint32_t write_elapsed_ms = 0;
    FRESULT result = FR_OK;
    UINT written = 0;
};

struct Core1UploadFinalizeResult {
    uint8_t transfer_id = 0;
    uint32_t close_elapsed_ms = 0;
    FRESULT result = FR_OK;
};

struct Core1TransferAbortResult {
    uint8_t transfer_id = 0;
    FRESULT close_result = FR_OK;
    FRESULT unlink_result = FR_OK;
    bool deleted_partial = false;
};

struct Core1DownloadOpenResult {
    uint8_t transfer_id = 0;
    uint32_t size = 0;
    FRESULT stat_result = FR_OK;
    FRESULT open_result = FR_OK;
    char filename[64]{};
};

struct Core1DownloadReadResult {
    uint8_t transfer_id = 0;
    uint32_t sequence = 0;
    uint16_t length = 0;
    FRESULT result = FR_OK;
    uint8_t payload[kCore1WorkerTransferChunkBytes]{};
};

struct Core1DownloadCloseResult {
    uint8_t transfer_id = 0;
    FRESULT result = FR_OK;
};

struct Core1FileListEntry {
    char name[64]{};
    uint32_t size = 0;
};

struct Core1FileListPageResult {
    uint8_t entry_count = 0;
    bool complete = false;
    FRESULT result = FR_OK;
    uint32_t total_count = 0;
    uint64_t free_bytes = 0;
    Core1FileListEntry entries[kCore1WorkerFileListPageEntries]{};
};

struct Core1DeleteFileResult {
    FRESULT result = FR_OK;
    char filename[64]{};
};

struct Core1FreeSpaceResult {
    FRESULT result = FR_OK;
    uint64_t free_bytes = 0;
};

struct Core1StorageHealthResult {
    bool healthy = false;
};

struct Core1Result {
    Core1ResultType type = Core1ResultType::None;
    Core1JobType source_job = Core1JobType::None;
    Core1UploadOpenResult open_upload{};
    Core1UploadChunkResult upload{};
    Core1UploadFinalizeResult finalize_upload{};
    Core1DownloadOpenResult open_download{};
    Core1DownloadReadResult read_download{};
    Core1DownloadCloseResult close_download{};
    Core1FileListPageResult file_list{};
    Core1DeleteFileResult delete_file{};
    Core1FreeSpaceResult free_space{};
    Core1StorageHealthResult health{};
    Core1TransferAbortResult abort_transfer{};
};

struct Core1WorkerSnapshot {
    bool started = false;
    bool lockout_ready = false;
    bool busy = false;
    bool has_foreground_work = false;
    bool has_background_only_work = false;
    bool has_exclusive_sd_work = false;
    bool has_preemptible_foreground_work = false;
    bool urgent_pending = false;
    Core1JobType active_job = Core1JobType::None;
    Core1JobIntent active_intent = Core1JobIntent::Unspecified;
    Core1JobType active_operation = Core1JobType::None;
    uint32_t heartbeat_counter = 0;
    std::size_t urgent_queue_count = 0;
    std::size_t control_queue_count = 0;
    std::size_t bulk_queue_count = 0;
    std::size_t result_queue_count = 0;
    std::size_t urgent_queue_high_water = 0;
    std::size_t control_queue_high_water = 0;
    std::size_t bulk_queue_high_water = 0;
    std::size_t result_queue_high_water = 0;
};
