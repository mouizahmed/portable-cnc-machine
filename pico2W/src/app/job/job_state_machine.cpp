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

bool JobStateMachine::has_selection() const {
    return selected_index_ >= 0;
}

int16_t JobStateMachine::selected_index() const {
    return selected_index_;
}

const FileEntry* JobStateMachine::selected_entry() const {
    if (!has_selection()) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(selected_index_)];
}

bool JobStateMachine::handle_event(JobEvent event, int16_t selected_index) {
    switch (event) {
        case JobEvent::SelectFile:
            if (selected_index < 0 || selected_index >= static_cast<int16_t>(entry_count_) || selected_index_ == selected_index) {
                return false;
            }
            selected_index_ = selected_index;
            state_ = JobState::FileSelected;
            return true;
        case JobEvent::StartRun:
            if (state_ != JobState::FileSelected) {
                return false;
            }
            state_ = JobState::Running;
            return true;
        case JobEvent::ClearSelection:
            if (selected_index_ < 0 && state_ == JobState::NoFileSelected) {
                return false;
            }
            selected_index_ = -1;
            state_ = JobState::NoFileSelected;
            return true;
    }

    return false;
}

bool JobStateMachine::can_run() const {
    return state_ == JobState::FileSelected;
}

void JobStateMachine::clear_files() {
    entry_count_ = 0;
    selected_index_ = -1;
    state_ = JobState::NoFileSelected;
}

bool JobStateMachine::add_file(const FileEntry& entry) {
    if (entry_count_ >= entries_.size()) {
        return false;
    }

    entries_[entry_count_] = entry;
    ++entry_count_;
    return true;
}
