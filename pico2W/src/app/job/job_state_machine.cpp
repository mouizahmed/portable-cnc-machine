#include "app/job/job_state_machine.h"

std::size_t JobStateMachine::count() const {
    return entry_count_;
}

const FileEntry& JobStateMachine::entry(std::size_t index) const {
    return entries_[index];
}

JobState JobStateMachine::state() const {
    return state_;
}

bool JobStateMachine::has_loaded_job() const {
    return loaded_index_ >= 0;
}

int16_t JobStateMachine::loaded_index() const {
    return loaded_index_;
}

const FileEntry* JobStateMachine::loaded_entry() const {
    if (!has_loaded_job()) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(loaded_index_)];
}

bool JobStateMachine::handle_event(JobEvent event, int16_t loaded_index) {
    switch (event) {
        case JobEvent::LoadFile:
            if (loaded_index < 0 || loaded_index >= static_cast<int16_t>(entry_count_) ||
                loaded_index_ == loaded_index) {
                return false;
            }
            loaded_index_ = loaded_index;
            state_ = JobState::JobLoaded;
            return true;
        case JobEvent::StartRun:
            if (state_ != JobState::JobLoaded) {
                return false;
            }
            state_ = JobState::Running;
            return true;
        case JobEvent::ClearLoadedFile:
            if (loaded_index_ < 0 && state_ == JobState::NoJobLoaded) {
                return false;
            }
            loaded_index_ = -1;
            state_ = JobState::NoJobLoaded;
            return true;
    }

    return false;
}

bool JobStateMachine::can_run() const {
    return state_ == JobState::JobLoaded;
}

void JobStateMachine::clear_files() {
    entry_count_ = 0;
    loaded_index_ = -1;
    state_ = JobState::NoJobLoaded;
}

bool JobStateMachine::add_file(const FileEntry& entry) {
    if (entry_count_ >= entries_.size()) {
        return false;
    }

    entries_[entry_count_] = entry;
    ++entry_count_;
    return true;
}
