#pragma once

#include <cstddef>
#include <cstdint>

#include "app/comm/motion_link_types.h"
#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_fsm.h"
#include "app/storage/storage_service.h"
#include "app/stream/job_stream_state_machine.h"
#include "app/worker/core1_worker.h"

enum class MotionLinkTransition : uint8_t {
    None,
    Connected,
    Disconnected,
};

struct PicoUartPollResult {
    bool machine_changed = false;
    bool job_changed = false;
    bool position_changed = false;
    bool job_progress = false;
    bool job_complete = false;
    bool job_error = false;
    uint32_t progress_line = 0;
    uint32_t progress_total = 0;
    char error_reason[32]{};
    MotionLinkTransition link_transition = MotionLinkTransition::None;
};

class PicoUartClient {
public:
    PicoUartClient(MachineFsm& machine_fsm,
                   JogStateMachine& jog_state_machine,
                   JobStateMachine& job_state_machine,
                   const StorageService& storage_service,
                   Core1Worker& worker,
                   JobStreamStateMachine& job_stream);

    PicoUartPollResult poll();
    bool select_file(int16_t index);
    bool upload_and_run_loaded_job();
    bool start_streaming_loaded_job();
    bool hold();
    bool resume();
    bool abort();
    bool estop();
    bool reset();
    bool jog_cancel();
    bool jog(JogAction action);
    bool jog_axis(char axis, float distance_mm, uint16_t feed_mm_min);
    bool home_all();
    bool zero_all();
    bool zero_axis(const char* axis);
    bool spindle_on(uint16_t rpm);
    bool spindle_off();
    const char* link_status() const;
    JobStreamState job_stream_state() const;
    MotionLinkSnapshot motion_snapshot() const;
    void handle_worker_result(const Core1Result& result);

private:
    static constexpr std::size_t kRxLineSize = 160;
    static constexpr uint32_t kProbeIntervalMs = 1000;
    static constexpr uint32_t kSilentLinkThresholdMs = 15000;
    static constexpr uint8_t kMaxMissedProbes = 2;
    static constexpr uint32_t kForegroundCommandTimeoutMs = 1500;

    MachineFsm& machine_fsm_;
    JogStateMachine& jog_state_machine_;
    JobStateMachine& job_state_machine_;
    const StorageService& storage_service_;
    Core1Worker& worker_;
    JobStreamStateMachine& job_stream_;

    bool transport_initialized_ = false;
    bool linked_ = false;
    uint32_t next_seq_ = 1;
    uint32_t last_rx_ms_ = 0;
    uint32_t last_probe_ms_ = 0;
    uint8_t missed_probes_ = 0;
    char rx_line_[kRxLineSize]{};
    std::size_t rx_length_ = 0;

    MotionLinkSnapshot motion_snapshot_{};
    uint32_t command_started_ms_ = 0;
    bool stream_batch_pending_ = false;
    bool stream_cancel_pending_ = false;
    bool stream_waiting_for_ok_ = false;
    bool stream_batch_complete_ = false;
    uint8_t stream_batch_line_count_ = 0;
    uint8_t stream_batch_next_line_ = 0;
    char stream_batch_lines_[kCore1WorkerStreamBatchLines][kCore1WorkerStreamLineBytes]{};
    uint32_t last_progress_emit_ms_ = 0;
    uint32_t stream_next_eligible_ms_ = 0;
    bool pending_job_progress_ = false;
    bool pending_job_complete_ = false;
    bool pending_job_error_ = false;
    uint32_t pending_progress_line_ = 0;
    uint32_t pending_progress_total_ = 0;
    char pending_error_reason_[32]{};

    void init_transport();
    bool send_probe();
    bool send_ping();
    bool send_command(const char* fmt, ...);
    bool send_raw_line(const char* line);
    uint32_t next_seq();

    PicoUartPollResult process_input(uint32_t now);
    PicoUartPollResult handle_line(const char* line, uint32_t now);
    PicoUartPollResult handle_ack_line(const char* line, uint32_t now);
    PicoUartPollResult handle_boot_line(const char* line, uint32_t now);
    PicoUartPollResult handle_grbl_state_line(const char* line, uint32_t now);
    PicoUartPollResult handle_error_line(const char* line, uint32_t now);

    void mark_valid_traffic(uint32_t now);
    PicoUartPollResult mark_link_connected(uint32_t now);
    PicoUartPollResult mark_link_disconnected();
    static void merge(PicoUartPollResult& into, const PicoUartPollResult& from);

    void note_command_started(MotionCommandType command, bool urgent, uint32_t now);
    void clear_command_in_flight();
    void mark_stream_fault();
    void mark_stream_fault(const char* reason);
    void cancel_stream(bool send_abort_command);
    bool request_next_stream_batch();
    bool send_next_stream_line(uint32_t now);
    void publish_stream_progress(bool force, uint32_t now);
    void finish_stream_if_ready();
    PicoUartPollResult handle_gcode_ok(uint32_t now);
    PicoUartPollResult handle_gcode_error(const char* line, uint32_t now);
    void service_background_telemetry(uint32_t now);

    static bool extract_token(const char* line, const char* key, char* out, std::size_t size);
    static bool extract_int(const char* line, const char* key, int& out);
    static bool extract_float(const char* line, const char* key, float& out);
    PicoUartPollResult handle_machine_state(const char* text, const char* substate, uint32_t now);
};
