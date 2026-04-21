#pragma once

#include <cstddef>
#include <cstdint>

#include "app/jog/jog_state_machine.h"
#include "app/job/loaded_job_storage.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_fsm.h"
#include "app/settings/machine_settings_store.h"
#include "app/storage/storage_transfer_fsm.h"
#include "app/storage/storage_service.h"
#include "drivers/sd_spi_card.h"
#include "protocol/protocol_defs.h"
#include "protocol/usb_cdc_transport.h"
#include "pico/sync.h"

extern "C" {
#include "ff.h"
}

class DesktopProtocol {
public:
    static constexpr size_t kTransferRawChunkSize = UsbCdcTransport::kMaxTransferPayloadSize;
    static constexpr uint8_t kUploadDataFrameType = 1;
    static constexpr uint8_t kUploadAckFrameType = 2;
    static constexpr uint8_t kDownloadDataFrameType = 3;
    static constexpr uint8_t kDownloadAckFrameType = 4;
    static constexpr uint8_t kCommandFrameType = 5;
    static constexpr uint8_t kResponseFrameType = 6;
    static constexpr uint8_t kEventFrameType = 7;

    DesktopProtocol(UsbCdcTransport& transport,
                    MachineFsm& msm,
                    JogStateMachine& jogs,
                    JobStateMachine& jobs,
                    LoadedJobStorage& loaded_job_storage,
                    MachineSettingsStore& machine_settings,
                    StorageService& storage,
                    SdSpiCard& sd);

    // Call every main-loop iteration. Reads pending USB input, dispatches commands.
    void poll();

    // Returns true (and clears the flag) if a protocol-driven FSM state change
    // occurred since the last call. Used by the main loop to trigger a screen re-render.
    bool upload_active() const { return transfer_.is_upload(); }
    bool storage_transfer_active() const { return transfer_.is_active(); }
    uint32_t upload_bytes_written() const { return transfer_.bytes_written(); }
    uint32_t upload_expected_size() const { return transfer_.expected_size(); }
    const char* upload_name() const { return transfer_.filename(); }

    bool consume_state_changed() {
        if (!state_changed_) return false;
        state_changed_ = false;
        return true;
    }

    // Returns true (and clears the flag) when a file was uploaded or deleted
    // since the last call. Used by the main loop to refresh the in-memory file
    // list and re-render the files screen.
    bool consume_file_list_changed() {
        if (!file_list_changed_) return false;
        file_list_changed_ = false;
        return true;
    }

    // -- Outbound protocol messages -------------------------------------------

    // Emit current state/capability/safety snapshot (call after any MSM transition)
    void emit_state_update(bool mark_changed = true);

    // Emit state snapshot
    void emit_state();

    // Emit capability snapshot
    void emit_caps();

    // Emit safety snapshot
    void emit_safety();

    // Emit loaded job snapshot
    void emit_job();

    // Emit current machine settings snapshot.
    void emit_machine_settings();

    // Emit position snapshot
    void emit_position();

    // Emit unsolicited event
    void emit_event(const char* name);
    void emit_event_kv(const char* name, const char* kv);

    // Emit command acknowledgement
    void emit_ok(const char* token);
    void emit_ok_kv(const char* token, const char* kv);

    // Emit command error
    void emit_error(const char* reason);

    // Abort an in-progress upload (callable from TFT touch events)
    void abort_upload() { handle_file_upload_abort(); }

    bool load_job_by_index(int16_t index);
    bool load_job_by_name(const char* name);
    bool unload_job();
    bool restore_persisted_job();

    // -- Storage event callbacks (called by PortableCncApp) -------------------
    void on_sd_mounted();
    void on_sd_removed();

private:
    static constexpr uint32_t kDownloadWindowSize = 8;
    static constexpr uint32_t kUploadAckStride = 8;
    static constexpr size_t kUploadQueueCapacity = 16;
    static constexpr size_t kUploadWorkerStackBytes = 8192;
    static constexpr uint32_t kDownloadResendIntervalMs = 750;

    struct UploadQueueEntry {
        uint8_t transfer_id = 0;
        uint32_t sequence = 0;
        uint16_t length = 0;
        uint32_t crc_after = 0xFFFFFFFFu;
        uint8_t payload[kTransferRawChunkSize]{};
    };

    struct UploadCommitResult {
        uint8_t transfer_id = 0;
        uint32_t sequence = 0;
        uint16_t length = 0;
        uint32_t crc_after = 0xFFFFFFFFu;
        uint32_t write_elapsed_ms = 0;
        FRESULT result = FR_OK;
        UINT written = 0;
    };

    UsbCdcTransport& transport_;
    MachineFsm& msm_;
    JogStateMachine& jogs_;
    JobStateMachine& jobs_;
    LoadedJobStorage& loaded_job_storage_;
    MachineSettingsStore& machine_settings_;
    StorageService& storage_;
    SdSpiCard& sd_;

    char line_[256];
    UsbCdcTransport::FramePacket frame_{};

    // Chunk decode buffers -- kept as members (not stack locals) to avoid
    // overflowing the default 2 KB Pico stack inside the deep f_write() call chain.

    // Download encode buffers -- kept as members to avoid stack pressure.
    uint8_t  chunk_raw_[kTransferRawChunkSize];

    // Set to true whenever emit_state_update() is called so the main loop can
    // trigger a touch screen re-render on protocol-driven state changes.
    bool state_changed_ = false;
    bool file_list_changed_ = false;
    uint32_t upload_profile_start_ms_ = 0;
    uint32_t upload_profile_prealloc_ms_ = 0;
    uint32_t upload_profile_write_ms_ = 0;
    uint32_t upload_profile_max_write_ms_ = 0;
    uint32_t upload_profile_close_ms_ = 0;
    uint32_t upload_profile_chunks_ = 0;
    UploadQueueEntry upload_queue_[kUploadQueueCapacity]{};
    size_t upload_queue_head_ = 0;
    size_t upload_queue_tail_ = 0;
    size_t upload_queue_count_ = 0;
    size_t upload_queue_high_water_ = 0;
    UploadCommitResult upload_results_[kUploadQueueCapacity]{};
    size_t upload_result_head_ = 0;
    size_t upload_result_tail_ = 0;
    size_t upload_result_count_ = 0;
    volatile bool upload_worker_enabled_ = false;
    volatile bool upload_worker_busy_ = false;
    critical_section_t upload_worker_lock_{};
    uint32_t upload_next_receive_sequence_ = 0;
    uint32_t upload_bytes_received_ = 0;
    uint32_t upload_receive_crc_running_ = 0xFFFFFFFFu;
    UploadQueueEntry upload_worker_entry_{};

    StorageTransferStateMachine transfer_;
    uint8_t next_transfer_id_ = 1;
    uint32_t current_request_seq_ = 0;
    uint8_t current_command_type_ = 0;
    bool handling_command_ = false;
    bool transfer_active() const { return transfer_.is_active(); }
    static DesktopProtocol* storage_worker_instance_;
    static volatile bool storage_worker_lockout_ready_;
    static uint32_t upload_worker_stack_[kUploadWorkerStackBytes / sizeof(uint32_t)];

// Command dispatch
    void dispatch_frame(const UsbCdcTransport::FramePacket& frame);
    void dispatch_command_frame(const UsbCdcTransport::FramePacket& frame);
    int16_t find_job_index_by_name(const char* name) const;
    bool try_load_job_by_index(int16_t index);
    bool try_unload_job();

    // Param helpers -- find "KEY=VALUE" in space-separated params string
    // Writes value into out (null-terminated). Returns true if found.
    static bool param_get(const char* params, const char* key, char* out, size_t max);
    static uint32_t param_get_u32(const char* params, const char* key, uint32_t def = 0);
    static int      param_get_int(const char* params, const char* key, int def = 0);

// Command handlers
    void handle_ping();
    void handle_info();
    void handle_status();
    void handle_home();
    void handle_jog(const char* params);
    void handle_jog_cancel();
    void handle_zero(const char* params);
    void handle_start();
    void handle_pause();
    void handle_resume();
    void handle_abort();
    void handle_estop();
    void handle_reset();
    void handle_spindle_on(const char* params);
    void handle_spindle_off();
    void handle_override(const char* params);
    void handle_file_list();
    void handle_file_load(const char* params);
    void handle_file_unload();
    void handle_file_delete(const char* params);
    void handle_file_upload(const char* params);
    void handle_file_upload_end(const char* params);
    void handle_file_upload_abort();
    void handle_file_download(const char* params);
    void handle_file_download_ack(const char* params);
    void handle_file_download_abort();
    void handle_settings_get();
    void handle_settings_set(const CmdSettingsSet& cmd);
    void fill_download_window();
    void send_next_download_chunk();
    void tick_transfer_retries();

// Upload helpers
    void upload_abort_cleanup();
    void handle_upload_chunk_data(uint8_t transfer_id, uint32_t sequence, const uint8_t* payload, uint16_t payload_len);
    void process_upload_results();
    bool enqueue_upload_chunk(uint8_t transfer_id, uint32_t sequence, const uint8_t* payload, uint16_t payload_len);
    void commit_upload_result(const UploadCommitResult& result);
    void reset_upload_queue();
    void storage_worker_loop();
    static void storage_worker_entry();
    void reset_upload_completion();
    void reset_download_completion();
    void send_upload_ready();
    void send_upload_chunk_ack(uint32_t sequence, uint32_t bytes_committed);
    void send_upload_complete();
    void send_download_ready();
    void send_download_complete();
    void abort_active_storage_transfer(bool clear_completion,
                                       bool delete_partial_upload_file,
                                       bool close_file = true);
    void emit_storage_error(StorageTransferError error,
                            StorageTransferOperation operation = StorageTransferOperation::None,
                            const char* kv = nullptr);

// Encoding helpers
    static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

    // Helper: inject MSM event and emit state update if changed
    void inject(MachineEvent ev);
};
