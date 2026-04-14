#pragma once

#include <cstddef>
#include <cstdint>

#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_state_machine.h"
#include "app/storage/storage_service.h"

class PicoUartClient {
public:
    PicoUartClient(MachineStateMachine& machine_state_machine,
                   JogStateMachine& jog_state_machine,
                   JobStateMachine& job_state_machine,
                   const StorageService& storage_service);

    bool poll();
    bool select_file(int16_t index);
    bool upload_and_run_selected_job();
    bool hold();
    bool resume();
    bool jog(JogAction action);
    bool home_all();
    bool zero_all();
    const char* link_status() const;

private:
    static constexpr std::size_t kRxLineSize = 160;
    static constexpr std::size_t kFileLineSize = 192;

    MachineStateMachine& machine_state_machine_;
    JogStateMachine& jog_state_machine_;
    JobStateMachine& job_state_machine_;
    const StorageService& storage_service_;
    bool hello_sent_ = false;
    bool linked_ = false;
    uint32_t next_seq_ = 1;
    char rx_line_[kRxLineSize]{};
    std::size_t rx_length_ = 0;

    bool send_hello();
    bool send_command(const char* fmt, ...);
    bool send_raw_line(const char* line);
    uint32_t next_seq();
    bool process_input();
    bool handle_line(const char* line);
    bool handle_event_line(const char* line);
    bool handle_error_line(const char* line);
    bool upload_job_file(const FileEntry& entry, int16_t index);
    static bool extract_token(const char* line, const char* key, char* out, std::size_t size);
    static bool extract_int(const char* line, const char* key, int& out);
    static bool extract_float(const char* line, const char* key, float& out);
    static void trim_line(char* line);
    static MachineState machine_state_from_text(const char* text);
    static JobState job_state_from_text(const char* text);
};
