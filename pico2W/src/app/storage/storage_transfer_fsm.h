#pragma once

#include <cstddef>
#include <cstdint>

#include "core/state_types.h"

extern "C" {
#include "ff.h"
}

// Transfer-level storage state. This is intentionally separate from
// StorageState in core/state_types.h, which only describes SD mount health.
enum class StorageTransferState : uint8_t {
    Idle,
    Listing,
    Loading,
    Deleting,
    UploadOpen,
    Uploading,
    UploadFinalizing,
    DownloadOpen,
    Downloading,
    Aborting,
    Faulted,
};

enum class StorageTransferEvent : uint8_t {
    FileListRequested,
    FileLoadRequested,
    FileUnloadRequested,
    FileDeleteRequested,
    UploadRequested,
    UploadChunkReceived,
    UploadEndRequested,
    DownloadRequested,
    DownloadAckReceived,
    AbortRequested,
    SdRemoved,
    StorageError,
    OperationComplete,
};

enum class StorageTransferError : uint8_t {
    None,
    Busy,
    NotAllowed,
    SdNotMounted,
    FileNotFound,
    InvalidFilename,
    InvalidSession,
    BadSequence,
    SizeMismatch,
    CrcMismatch,
    ReadFail,
    WriteFail,
    NoSpace,
    Aborted,
};

enum class StorageTransferOperation : uint8_t {
    None,
    List,
    Load,
    Unload,
    Delete,
    Upload,
    Download,
};

struct StorageTransferContext {
    StorageTransferState state = StorageTransferState::Idle;
    StorageTransferOperation operation = StorageTransferOperation::None;
    StorageTransferError last_error = StorageTransferError::None;
    char filename[64]{};
    uint8_t session_id = 0;
    uint32_t expected_size = 0;
    uint32_t bytes_written = 0;
    uint32_t bytes_sent = 0;
    uint32_t expected_sequence = 0;
    uint32_t last_ack_sequence = 0;
    uint32_t crc_running = 0xFFFFFFFFu;
    uint32_t retry_count = 0;
    bool awaiting_ack = false;
    uint16_t last_chunk_length = 0;
    uint32_t last_send_ms = 0;
    FIL file{};
    bool completion_valid = false;
    StorageTransferOperation completion_operation = StorageTransferOperation::None;
    char completion_name[64]{};
    uint32_t completion_size = 0;
    uint32_t completion_crc = 0;
    uint8_t completion_session_id = 0;
    uint32_t completion_last_seq = 0;
};

class StorageTransferStateMachine {
public:
    const StorageTransferContext& context() const { return ctx_; }

    StorageTransferState state() const { return ctx_.state; }
    StorageTransferOperation operation() const { return ctx_.operation; }
    StorageTransferError last_error() const { return ctx_.last_error; }
    bool is_active() const;
    bool is_upload() const;
    bool is_download() const;

    const char* filename() const { return ctx_.filename; }
    uint8_t session_id() const { return ctx_.session_id; }
    uint32_t expected_size() const { return ctx_.expected_size; }
    uint32_t bytes_written() const { return ctx_.bytes_written; }
    uint32_t bytes_sent() const { return ctx_.bytes_sent; }
    uint32_t expected_sequence() const { return ctx_.expected_sequence; }
    uint32_t last_ack_sequence() const { return ctx_.last_ack_sequence; }
    uint32_t crc_running() const { return ctx_.crc_running; }
    uint32_t retry_count() const { return ctx_.retry_count; }
    bool awaiting_ack() const { return ctx_.awaiting_ack; }
    uint16_t last_chunk_length() const { return ctx_.last_chunk_length; }
    uint32_t last_send_ms() const { return ctx_.last_send_ms; }
    FIL& file() { return ctx_.file; }
    const FIL& file() const { return ctx_.file; }

    bool begin_listing(MachineOperationState machine_state);
    bool begin_loading(MachineOperationState machine_state, const char* filename);
    bool begin_unload(MachineOperationState machine_state);
    bool begin_deleting(MachineOperationState machine_state, const char* filename);
    bool begin_upload(MachineOperationState machine_state, const char* filename, uint32_t expected_size, uint8_t session_id);
    bool begin_download(MachineOperationState machine_state, const char* filename, uint32_t expected_size, uint8_t session_id);
    bool begin_aborting();

    void transition(StorageTransferState next);
    void set_error(StorageTransferError error, bool fault = false);
    void clear_error();
    void set_expected_size(uint32_t expected_size) { ctx_.expected_size = expected_size; }
    void set_expected_sequence(uint32_t sequence) { ctx_.expected_sequence = sequence; }
    void set_last_ack_sequence(uint32_t sequence) { ctx_.last_ack_sequence = sequence; }
    void set_crc_running(uint32_t crc) { ctx_.crc_running = crc; }
    void set_retry_count(uint32_t retries) { ctx_.retry_count = retries; }
    void note_retry() { ++ctx_.retry_count; }
    void set_awaiting_ack(bool awaiting) { ctx_.awaiting_ack = awaiting; }
    void set_last_chunk_length(uint16_t length) { ctx_.last_chunk_length = length; }
    void set_last_send_ms(uint32_t time_ms) { ctx_.last_send_ms = time_ms; }
    void set_session_id(uint8_t session_id) { ctx_.session_id = session_id; }
    void note_upload_chunk_committed(uint32_t sequence, uint32_t bytes_written, uint32_t crc_running);
    void note_download_chunk_sent(uint32_t sequence, uint16_t chunk_length, uint32_t bytes_sent, uint32_t crc_running, uint32_t send_time_ms);
    void note_download_ack(uint32_t sequence);
    void set_completion(StorageTransferOperation operation,
                        const char* filename,
                        uint32_t size,
                        uint32_t crc,
                        uint8_t session_id,
                        uint32_t last_seq);
    void clear_completion();
    void finish_operation();
    void reset();

private:
    StorageTransferContext ctx_{};

    bool begin_operation(StorageTransferOperation operation,
                         MachineOperationState machine_state,
                         StorageTransferState state,
                         const char* filename,
                         uint32_t expected_size,
                         uint8_t session_id);
    static void copy_name(const char* input, char* output, std::size_t output_size);
};

const char* storage_transfer_state_text(StorageTransferState state);
const char* storage_transfer_event_text(StorageTransferEvent event);
const char* storage_transfer_error_text(StorageTransferError error);
const char* storage_transfer_operation_text(StorageTransferOperation operation);

bool storage_transfer_state_is_active(StorageTransferState state);
bool storage_transfer_state_is_upload(StorageTransferState state);
bool storage_transfer_state_is_download(StorageTransferState state);
bool storage_transfer_machine_state_is_allowed(MachineOperationState state);
