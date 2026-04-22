#pragma once

#include <cstddef>
#include <cstdint>

enum class JobStreamState : uint8_t {
    Idle,
    Preparing,
    Beginning,
    Streaming,
    PausedByHold,
    Cancelling,
    Complete,
    Faulted,
};

class JobStreamStateMachine {
public:
    JobStreamState state() const { return state_; }
    bool is_active() const;
    int16_t loaded_index() const { return loaded_index_; }
    uint32_t total_lines() const { return total_lines_; }
    uint32_t lines_sent() const { return lines_sent_; }
    const char* filename() const { return filename_; }

    void reset();
    void start_prepare(int16_t loaded_index, const char* filename);
    void note_prepare_ready(uint32_t total_lines);
    void note_beginning();
    void note_lines_sent(uint32_t count);
    void note_run_started();
    void note_hold();
    void note_resume();
    void note_cancelling();
    void note_complete();
    void note_fault();

private:
    static void copy_text(char* dest, std::size_t size, const char* src);

    JobStreamState state_ = JobStreamState::Idle;
    int16_t loaded_index_ = -1;
    uint32_t total_lines_ = 0;
    uint32_t lines_sent_ = 0;
    char filename_[64]{};
};
