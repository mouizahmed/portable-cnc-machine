#include "app/job/job_state_machine.h"

#include <array>

namespace {
std::array<FileEntry, 5> kFileEntries{{
    {"DEMO.NC", "READY TO RUN", "24 KB", "T1 1/8 EM", "G54"},
    {"FRAME.NC", "ALUMINUM PLATE", "31 KB", "T2 1/4 EM", "G54"},
    {"PLATE.NC", "DRILL / CONTOUR", "18 KB", "T3 DRILL", "G55"},
    {"HOLES.NC", "PECK CYCLE", "9 KB", "T3 DRILL", "G54"},
    {"POCKET.NC", "ROUGHING PASS", "42 KB", "T2 1/4 EM", "G56"},
}};
}  // namespace

std::size_t JobStateMachine::count() const {
    return kFileEntries.size();
}

const FileEntry& JobStateMachine::entry(std::size_t index) const {
    return kFileEntries[index];
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
    return &kFileEntries[static_cast<std::size_t>(selected_index_)];
}

bool JobStateMachine::handle_event(JobEvent event, int16_t selected_index) {
    switch (event) {
        case JobEvent::SelectFile:
            if (selected_index < 0 || selected_index >= static_cast<int16_t>(kFileEntries.size()) || selected_index_ == selected_index) {
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
