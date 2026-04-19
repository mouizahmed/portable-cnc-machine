#pragma once

#include <cstddef>
#include <cstdint>

#include "app/jog/jog_state_machine.h"
#include "app/job/loaded_job_storage.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_fsm.h"
#include "app/storage/storage_service.h"
#include "drivers/sd_spi_card.h"
#include "protocol/usb_cdc_transport.h"

extern "C" {
#include "ff.h"
}

class DesktopProtocol {
public:
    static constexpr size_t kTransferRawChunkSize = 96;
    static constexpr uint8_t kUploadDataFrameType = 1;
    static constexpr uint8_t kUploadAckFrameType = 2;
    static constexpr uint8_t kDownloadDataFrameType = 3;
    static constexpr uint8_t kDownloadAckFrameType = 4;

    DesktopProtocol(UsbCdcTransport& transport,
                    MachineFsm& msm,
                    JogStateMachine& jogs,
                    JobStateMachine& jobs,
                    LoadedJobStorage& loaded_job_storage,
                    StorageService& storage,
                    SdSpiCard& sd);

    // Call every main-loop iteration. Reads pending USB input, dispatches commands.
    void poll();

    // Returns true (and clears the flag) if a protocol-driven FSM state change
    // occurred since the last call. Used by the main loop to trigger a screen re-render.
    bool upload_active() const { return upload_.active; }
    uint32_t upload_bytes_written() const { return upload_.bytes_written; }
    uint32_t upload_expected_size() const { return upload_.expected_size; }
    const char* upload_name() const { return upload_.name; }

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

    // Emit current @STATE, @CAPS, @SAFETY (call after any MSM transition)
    void emit_state_update(bool mark_changed = true);

    // Emit @STATE <state>
    void emit_state();

    // Emit @CAPS ...
    void emit_caps();

    // Emit @SAFETY <level>
    void emit_safety();

    // Emit @JOB NAME=<filename|NONE>
    void emit_job();

    // Emit @POS MX=... MY=... MZ=... WX=... WY=... WZ=...
    void emit_position();

    // Emit @EVENT <name> [key=val ...]
    void emit_event(const char* name);
    void emit_event_kv(const char* name, const char* kv);

    // Emit @OK <token> [key=val ...]
    void emit_ok(const char* token);
    void emit_ok_kv(const char* token, const char* kv);

    // Emit @ERROR <reason>
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
    static constexpr uint32_t kDownloadResendIntervalMs = 750;

    UsbCdcTransport& transport_;
    MachineFsm& msm_;
    JogStateMachine& jogs_;
    JobStateMachine& jobs_;
    LoadedJobStorage& loaded_job_storage_;
    StorageService& storage_;
    SdSpiCard& sd_;

    char line_[256];

    // Chunk decode buffers -- kept as members (not stack locals) to avoid
    // overflowing the default 2 KB Pico stack inside the deep f_write() call chain.

    // Download encode buffers -- kept as members to avoid stack pressure.
    uint8_t  chunk_raw_[kTransferRawChunkSize];
    char     chunk_hex_[kTransferRawChunkSize * 2 + 1];

    // Set to true whenever emit_state_update() is called so the main loop can
    // trigger a touch screen re-render on protocol-driven state changes.
    bool state_changed_ = false;
    bool file_list_changed_ = false;

    // -- Upload state ----------------------------------------------------------
    struct UploadState {
        bool     active         = false;
        char     name[64]       = {};
        uint32_t expected_size  = 0;
        uint32_t bytes_written  = 0;
        uint32_t expected_seq   = 0;
        uint32_t crc_running    = 0xFFFFFFFFu;
        uint8_t  transfer_id    = 0;
        bool     last_ack_valid = false;
        uint32_t last_ack_seq   = 0;
        uint32_t last_ack_bytes = 0;
        FIL      file           = {};
        bool     completion_valid = false;
        char     completion_name[64] = {};
        uint32_t completion_size = 0;
        uint32_t completion_crc  = 0;
        uint8_t  completion_transfer_id = 0;
    } upload_;

// Download state
    struct DownloadState {
        bool     active         = false;
        char     name[64]       = {};
        uint32_t total_size     = 0;
        uint32_t bytes_sent     = 0;
        uint32_t crc_running    = 0xFFFFFFFFu;
        uint32_t next_seq       = 0;
        uint8_t  transfer_id    = 0;
        FIL      file           = {};
        bool     awaiting_ack   = false;
        uint32_t last_chunk_seq = 0;
        uint16_t last_chunk_len = 0;
        uint32_t last_send_ms   = 0;
        bool     completion_valid = false;
        char     completion_name[64] = {};
        uint32_t completion_crc  = 0;
        uint32_t completion_last_seq = 0;
        uint8_t  completion_transfer_id = 0;
    } download_;
    uint8_t next_transfer_id_ = 1;
    bool transfer_active() const { return upload_.active || download_.active; }

// Command dispatch
    void dispatch(const char* line);
    void dispatch_frame(const UsbCdcTransport::FramePacket& frame);
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
    void handle_file_upload_chunk(const char* params);
    void handle_file_upload_end(const char* params);
    void handle_file_upload_abort();
    void handle_file_download(const char* params);
    void handle_file_download_ack(const char* params);
    void handle_file_download_abort();
    void send_next_download_chunk();
    void tick_transfer_retries();

// Upload helpers
    void upload_abort_cleanup();
    void handle_upload_chunk_data(uint8_t transfer_id, uint32_t sequence, const uint8_t* payload, uint16_t payload_len);
    void reset_upload_completion();
    void reset_download_completion();
    void send_upload_ready();
    void send_upload_chunk_ack(uint32_t sequence, uint32_t bytes_committed);
    void send_upload_complete();
    void send_download_ready();
    void send_download_complete();
    void resend_download_chunk();
    void send_download_chunk_line();

// Encoding helpers
    static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);
    static void hex_encode(const uint8_t* data, size_t len, char* out, size_t out_size);
    static bool hex_decode(const char* hex, uint8_t* out, size_t out_size, uint16_t* out_len);

    // Helper: inject MSM event and emit state update if changed
    void inject(MachineEvent ev);
};
