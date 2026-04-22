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
    bool hold();
    bool resume();
    bool abort();
    bool jog(JogAction action);
    bool home_all();
    bool zero_all();
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

    void init_transport();
    bool send_probe();
    bool send_ping();
    bool send_hello();
    bool send_command(const char* fmt, ...);
    bool send_raw_line(const char* line);
    uint32_t next_seq();

    PicoUartPollResult process_input(uint32_t now);
    PicoUartPollResult handle_line(const char* line, uint32_t now);
    PicoUartPollResult handle_ack_line(const char* line, uint32_t now);
    PicoUartPollResult handle_boot_line(const char* line, uint32_t now);
    PicoUartPollResult handle_grbl_state_line(const char* line, uint32_t now);
    PicoUartPollResult handle_event_line(const char* line, uint32_t now);
    PicoUartPollResult handle_error_line(const char* line, uint32_t now);

    void mark_valid_traffic(uint32_t now);
    PicoUartPollResult mark_link_connected(uint32_t now);
    PicoUartPollResult mark_link_disconnected();
    static void merge(PicoUartPollResult& into, const PicoUartPollResult& from);

    void note_command_started(MotionCommandType command, bool urgent, uint32_t now);
    void clear_command_in_flight();
    void mark_stream_fault();
    void cancel_stream(bool send_abort_command);
    bool request_next_stream_batch();
    bool send_job_begin_sequence(int16_t index, const char* name, uint32_t line_count, uint32_t now);
    void service_background_telemetry(uint32_t now);

    static bool extract_token(const char* line, const char* key, char* out, std::size_t size);
    static bool extract_int(const char* line, const char* key, int& out);
    static bool extract_float(const char* line, const char* key, float& out);
    PicoUartPollResult handle_machine_state(const char* text, const char* substate, uint32_t now);
    PicoUartPollResult handle_job_state(const char* text, int index, uint32_t now);
};
