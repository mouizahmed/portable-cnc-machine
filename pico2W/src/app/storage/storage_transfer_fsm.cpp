#include "app/storage/storage_transfer_fsm.h"

#include <cstring>

const char* storage_transfer_state_text(StorageTransferState state) {
    switch (state) {
        case StorageTransferState::Idle:              return "IDLE";
        case StorageTransferState::Listing:           return "LISTING";
        case StorageTransferState::Loading:           return "LOADING";
        case StorageTransferState::Deleting:          return "DELETING";
        case StorageTransferState::UploadOpen:        return "UPLOAD_OPEN";
        case StorageTransferState::Uploading:         return "UPLOADING";
        case StorageTransferState::UploadFinalizing:  return "UPLOAD_FINALIZING";
        case StorageTransferState::DownloadOpen:      return "DOWNLOAD_OPEN";
        case StorageTransferState::Downloading:       return "DOWNLOADING";
        case StorageTransferState::Aborting:          return "ABORTING";
        case StorageTransferState::Faulted:           return "FAULTED";
    }
    return "UNKNOWN";
}

const char* storage_transfer_event_text(StorageTransferEvent event) {
    switch (event) {
        case StorageTransferEvent::FileListRequested:   return "FILE_LIST_REQUESTED";
        case StorageTransferEvent::FileLoadRequested:   return "FILE_LOAD_REQUESTED";
        case StorageTransferEvent::FileUnloadRequested: return "FILE_UNLOAD_REQUESTED";
        case StorageTransferEvent::FileDeleteRequested: return "FILE_DELETE_REQUESTED";
        case StorageTransferEvent::UploadRequested:     return "UPLOAD_REQUESTED";
        case StorageTransferEvent::UploadChunkReceived: return "UPLOAD_CHUNK_RECEIVED";
        case StorageTransferEvent::UploadEndRequested:  return "UPLOAD_END_REQUESTED";
        case StorageTransferEvent::DownloadRequested:   return "DOWNLOAD_REQUESTED";
        case StorageTransferEvent::DownloadAckReceived: return "DOWNLOAD_ACK_RECEIVED";
        case StorageTransferEvent::AbortRequested:      return "ABORT_REQUESTED";
        case StorageTransferEvent::SdRemoved:           return "SD_REMOVED";
        case StorageTransferEvent::StorageError:        return "STORAGE_ERROR";
        case StorageTransferEvent::OperationComplete:   return "OPERATION_COMPLETE";
    }
    return "UNKNOWN";
}

const char* storage_transfer_error_text(StorageTransferError error) {
    switch (error) {
        case StorageTransferError::None:            return "NONE";
        case StorageTransferError::Busy:            return "BUSY";
        case StorageTransferError::NotAllowed:      return "NOT_ALLOWED";
        case StorageTransferError::SdNotMounted:    return "SD_NOT_MOUNTED";
        case StorageTransferError::FileNotFound:    return "FILE_NOT_FOUND";
        case StorageTransferError::InvalidFilename: return "INVALID_FILENAME";
        case StorageTransferError::InvalidSession:  return "INVALID_SESSION";
        case StorageTransferError::BadSequence:     return "BAD_SEQUENCE";
        case StorageTransferError::SizeMismatch:    return "SIZE_MISMATCH";
        case StorageTransferError::CrcMismatch:     return "CRC_MISMATCH";
        case StorageTransferError::ReadFail:        return "READ_FAIL";
        case StorageTransferError::WriteFail:       return "WRITE_FAIL";
        case StorageTransferError::NoSpace:         return "NO_SPACE";
        case StorageTransferError::Aborted:         return "ABORTED";
    }
    return "UNKNOWN";
}

const char* storage_transfer_operation_text(StorageTransferOperation operation) {
    switch (operation) {
        case StorageTransferOperation::None:     return "NONE";
        case StorageTransferOperation::List:     return "LIST";
        case StorageTransferOperation::Load:     return "LOAD";
        case StorageTransferOperation::Unload:   return "UNLOAD";
        case StorageTransferOperation::Delete:   return "DELETE";
        case StorageTransferOperation::Upload:   return "UPLOAD";
        case StorageTransferOperation::Download: return "DOWNLOAD";
    }
    return "UNKNOWN";
}

bool storage_transfer_state_is_active(StorageTransferState state) {
    return state != StorageTransferState::Idle &&
           state != StorageTransferState::Faulted;
}

bool storage_transfer_state_is_upload(StorageTransferState state) {
    return state == StorageTransferState::UploadOpen ||
           state == StorageTransferState::Uploading ||
           state == StorageTransferState::UploadFinalizing;
}

bool storage_transfer_state_is_download(StorageTransferState state) {
    return state == StorageTransferState::DownloadOpen ||
           state == StorageTransferState::Downloading;
}

bool storage_transfer_machine_state_is_allowed(MachineOperationState state) {
    return state == MachineOperationState::Idle ||
           state == MachineOperationState::TeensyDisconnected;
}

bool StorageTransferStateMachine::is_active() const {
    return storage_transfer_state_is_active(ctx_.state);
}

bool StorageTransferStateMachine::is_upload() const {
    return storage_transfer_state_is_upload(ctx_.state);
}

bool StorageTransferStateMachine::is_download() const {
    return storage_transfer_state_is_download(ctx_.state);
}

bool StorageTransferStateMachine::begin_listing(MachineOperationState machine_state) {
    return begin_operation(StorageTransferOperation::List,
                           machine_state,
                           StorageTransferState::Listing,
                           nullptr,
                           0,
                           0);
}

bool StorageTransferStateMachine::begin_loading(MachineOperationState machine_state, const char* filename) {
    return begin_operation(StorageTransferOperation::Load,
                           machine_state,
                           StorageTransferState::Loading,
                           filename,
                           0,
                           0);
}

bool StorageTransferStateMachine::begin_unload(MachineOperationState machine_state) {
    return begin_operation(StorageTransferOperation::Unload,
                           machine_state,
                           StorageTransferState::Loading,
                           nullptr,
                           0,
                           0);
}

bool StorageTransferStateMachine::begin_deleting(MachineOperationState machine_state, const char* filename) {
    return begin_operation(StorageTransferOperation::Delete,
                           machine_state,
                           StorageTransferState::Deleting,
                           filename,
                           0,
                           0);
}

bool StorageTransferStateMachine::begin_upload(MachineOperationState machine_state,
                                               const char* filename,
                                               uint32_t expected_size,
                                               uint8_t session_id) {
    return begin_operation(StorageTransferOperation::Upload,
                           machine_state,
                           StorageTransferState::UploadOpen,
                           filename,
                           expected_size,
                           session_id);
}

bool StorageTransferStateMachine::begin_download(MachineOperationState machine_state,
                                                 const char* filename,
                                                 uint32_t expected_size,
                                                 uint8_t session_id) {
    return begin_operation(StorageTransferOperation::Download,
                           machine_state,
                           StorageTransferState::DownloadOpen,
                           filename,
                           expected_size,
                           session_id);
}

bool StorageTransferStateMachine::begin_aborting() {
    if (!is_active()) {
        return false;
    }
    transition(StorageTransferState::Aborting);
    clear_error();
    return true;
}

void StorageTransferStateMachine::transition(StorageTransferState next) {
    ctx_.state = next;
}

void StorageTransferStateMachine::set_error(StorageTransferError error, bool fault) {
    ctx_.last_error = error;
    if (fault) {
        ctx_.state = StorageTransferState::Faulted;
    }
}

void StorageTransferStateMachine::clear_error() {
    ctx_.last_error = StorageTransferError::None;
}

void StorageTransferStateMachine::note_upload_chunk_committed(uint32_t sequence,
                                                              uint32_t bytes_written,
                                                              uint32_t crc_running) {
    ctx_.expected_sequence = sequence + 1;
    ctx_.last_ack_sequence = sequence;
    ctx_.bytes_written = bytes_written;
    ctx_.crc_running = crc_running;
    ctx_.retry_count = 0;
}

void StorageTransferStateMachine::note_download_chunk_sent(uint32_t sequence,
                                                           uint16_t chunk_length,
                                                           uint32_t bytes_sent,
                                                           uint32_t crc_running,
                                                           uint32_t send_time_ms) {
    ctx_.expected_sequence = sequence + 1;
    ctx_.bytes_sent = bytes_sent;
    ctx_.crc_running = crc_running;
    ctx_.awaiting_ack = true;
    ctx_.last_ack_sequence = sequence;
    ctx_.last_chunk_length = chunk_length;
    ctx_.last_send_ms = send_time_ms;
}

void StorageTransferStateMachine::note_download_ack(uint32_t sequence) {
    ctx_.last_ack_sequence = sequence;
    ctx_.awaiting_ack = false;
    ctx_.retry_count = 0;
}

void StorageTransferStateMachine::set_completion(StorageTransferOperation operation,
                                                 const char* filename,
                                                 uint32_t size,
                                                 uint32_t crc,
                                                 uint8_t session_id,
                                                 uint32_t last_seq) {
    ctx_.completion_valid = true;
    ctx_.completion_operation = operation;
    copy_name(filename, ctx_.completion_name, sizeof(ctx_.completion_name));
    ctx_.completion_size = size;
    ctx_.completion_crc = crc;
    ctx_.completion_session_id = session_id;
    ctx_.completion_last_seq = last_seq;
}

void StorageTransferStateMachine::clear_completion() {
    ctx_.completion_valid = false;
    ctx_.completion_operation = StorageTransferOperation::None;
    ctx_.completion_name[0] = '\0';
    ctx_.completion_size = 0;
    ctx_.completion_crc = 0;
    ctx_.completion_session_id = 0;
    ctx_.completion_last_seq = 0;
}

void StorageTransferStateMachine::finish_operation() {
    ctx_.state = StorageTransferState::Idle;
    ctx_.operation = StorageTransferOperation::None;
    ctx_.filename[0] = '\0';
    ctx_.session_id = 0;
    ctx_.expected_size = 0;
    ctx_.bytes_written = 0;
    ctx_.bytes_sent = 0;
    ctx_.expected_sequence = 0;
    ctx_.last_ack_sequence = 0;
    ctx_.crc_running = 0xFFFFFFFFu;
    ctx_.retry_count = 0;
    ctx_.awaiting_ack = false;
    ctx_.last_chunk_length = 0;
    ctx_.last_send_ms = 0;
    ctx_.file = FIL{};
    clear_error();
}

void StorageTransferStateMachine::reset() {
    ctx_ = StorageTransferContext{};
}

bool StorageTransferStateMachine::begin_operation(StorageTransferOperation operation,
                                                  MachineOperationState machine_state,
                                                  StorageTransferState state,
                                                  const char* filename,
                                                  uint32_t expected_size,
                                                  uint8_t session_id) {
    if (is_active()) {
        set_error(StorageTransferError::Busy);
        return false;
    }

    if (!storage_transfer_machine_state_is_allowed(machine_state)) {
        set_error(StorageTransferError::NotAllowed);
        return false;
    }

    ctx_ = StorageTransferContext{};
    ctx_.operation = operation;
    ctx_.state = state;
    ctx_.expected_size = expected_size;
    ctx_.session_id = session_id;
    copy_name(filename, ctx_.filename, sizeof(ctx_.filename));
    return true;
}

void StorageTransferStateMachine::copy_name(const char* input, char* output, std::size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return;
    }

    output[0] = '\0';
    if (input == nullptr) {
        return;
    }

    std::strncpy(output, input, output_size - 1);
    output[output_size - 1] = '\0';
}
