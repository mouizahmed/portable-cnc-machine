#include "app/stream/job_stream_state_machine.h"

#include <cstddef>
#include <cstring>

namespace {
bool state_is_active(JobStreamState state) {
    return state == JobStreamState::Preparing ||
           state == JobStreamState::Beginning ||
           state == JobStreamState::Streaming ||
           state == JobStreamState::PausedByHold ||
           state == JobStreamState::Cancelling;
}
}  // namespace

bool JobStreamStateMachine::is_active() const {
    return state_is_active(state_);
}

void JobStreamStateMachine::reset() {
    state_ = JobStreamState::Idle;
    loaded_index_ = -1;
    total_lines_ = 0;
    lines_sent_ = 0;
    filename_[0] = '\0';
}

void JobStreamStateMachine::start_prepare(int16_t loaded_index, const char* filename) {
    reset();
    state_ = JobStreamState::Preparing;
    loaded_index_ = loaded_index;
    copy_text(filename_, sizeof(filename_), filename);
}

void JobStreamStateMachine::note_prepare_ready(uint32_t total_lines) {
    total_lines_ = total_lines;
}

void JobStreamStateMachine::note_beginning() {
    state_ = JobStreamState::Beginning;
}

void JobStreamStateMachine::note_lines_sent(uint32_t count) {
    lines_sent_ += count;
}

void JobStreamStateMachine::note_run_started() {
    state_ = JobStreamState::Streaming;
}

void JobStreamStateMachine::note_hold() {
    if (state_is_active(state_)) {
        state_ = JobStreamState::PausedByHold;
    }
}

void JobStreamStateMachine::note_resume() {
    if (state_ == JobStreamState::PausedByHold) {
        state_ = JobStreamState::Streaming;
    }
}

void JobStreamStateMachine::note_cancelling() {
    if (state_is_active(state_) || state_ == JobStreamState::Faulted) {
        state_ = JobStreamState::Cancelling;
    }
}

void JobStreamStateMachine::note_complete() {
    state_ = JobStreamState::Complete;
}

void JobStreamStateMachine::note_fault() {
    state_ = JobStreamState::Faulted;
}

void JobStreamStateMachine::copy_text(char* dest, std::size_t size, const char* src) {
    if (dest == nullptr || size == 0) {
        return;
    }
    dest[0] = '\0';
    if (src == nullptr) {
        return;
    }

    std::strncpy(dest, src, size - 1u);
    dest[size - 1u] = '\0';
}
