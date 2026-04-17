#pragma once

#include <cstdint>

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
    DesktopProtocol(UsbCdcTransport& transport,
                    MachineFsm& msm,
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Outbound protocol messages Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

    // Emit current @STATE, @CAPS, @SAFETY (call after any MSM transition)
    void emit_state_update();

    // Emit @STATE <state>
    void emit_state();

    // Emit @CAPS ...
    void emit_caps();

    // Emit @SAFETY <level>
    void emit_safety();

    // Emit @JOB NAME=<filename|NONE>
    void emit_job();

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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Storage event callbacks (called by PortableCncApp) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    void on_sd_mounted();
    void on_sd_removed();

private:
    UsbCdcTransport& transport_;
    MachineFsm& msm_;
    JobStateMachine& jobs_;
    LoadedJobStorage& loaded_job_storage_;
    StorageService& storage_;
    SdSpiCard& sd_;

    char line_[512];  // must fit largest line: @CHUNK SEQ=n DATA=<256 base64 chars> ~280 chars

    // Chunk decode buffers Ã¢â‚¬â€ kept as members (not stack locals) to avoid
    // overflowing the default 2 KB Pico stack inside the deep f_write() call chain.
    char     chunk_b64_[512];
    uint8_t  chunk_decoded_[384];

    // Download encode buffers Ã¢â‚¬â€ kept as members to avoid stack pressure.
    char     chunk_encode_[260];   // base64 output: 192 raw bytes Ã¢â€ â€™ 256 chars + null
    uint8_t  chunk_raw_[192];      // raw file read buffer

    // Set to true whenever emit_state_update() is called so the main loop can
    // trigger a touch screen re-render on protocol-driven state changes.
    bool state_changed_ = false;
    bool file_list_changed_ = false;

    // Ã¢â€â‚¬Ã¢â€â‚¬ Upload state Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    struct UploadState {
        bool     active         = false;
        char     name[64]       = {};
        uint32_t expected_size  = 0;
        uint32_t bytes_written  = 0;
        int      expected_seq   = 0;
        uint32_t crc_running    = 0xFFFFFFFFu;
        FIL      file           = {};
    } upload_;

    // Ã¢â€â‚¬Ã¢â€â‚¬ Download state Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    struct DownloadState {
        bool     active       = false;
        char     name[64]     = {};
        uint32_t total_size   = 0;
        uint32_t bytes_sent   = 0;
        uint32_t crc_running  = 0xFFFFFFFFu;
        int      next_seq     = 0;
        FIL      file         = {};
    } download_;

    // Ã¢â€â‚¬Ã¢â€â‚¬ Command dispatch Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    void dispatch(const char* line);
    int16_t find_job_index_by_name(const char* name) const;
    bool try_load_job_by_index(int16_t index);
    bool try_unload_job();

    // Param helpers Ã¢â‚¬â€ find "KEY=VALUE" in space-separated params string
    // Writes value into out (null-terminated). Returns true if found.
    static bool param_get(const char* params, const char* key, char* out, size_t max);
    static uint32_t param_get_u32(const char* params, const char* key, uint32_t def = 0);
    static int      param_get_int(const char* params, const char* key, int def = 0);

    // Ã¢â€â‚¬Ã¢â€â‚¬ Command handlers Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
    void handle_chunk(const char* params);
    void handle_file_upload_end(const char* params);
    void handle_file_upload_abort();
    void handle_file_download(const char* params);
    void handle_download_ack(const char* params);
    void send_next_download_chunk();

    // Ã¢â€â‚¬Ã¢â€â‚¬ Upload helpers Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    void upload_abort_cleanup();

    // Ã¢â€â‚¬Ã¢â€â‚¬ Encoding helpers Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    static bool base64_decode(const char* in, uint8_t* out, size_t* out_len, size_t out_cap);
    static void base64_encode(const uint8_t* in, size_t len, char* out, size_t out_cap);
    static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

    // Helper: inject MSM event and emit state update if changed
    void inject(MachineEvent ev);
};
