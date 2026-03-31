#pragma once

#include <cstddef>
#include <cstdint>

#include "core/state_types.h"

enum class JobEvent : uint8_t {
    SelectFile,
    StartRun,
    ClearSelection,
};

struct FileEntry {
    const char* name;
    const char* summary;
    const char* size_text;
    const char* tool_text;
    const char* zero_text;
};

class JobStateMachine {
public:
    std::size_t count() const;
    const FileEntry& entry(std::size_t index) const;

    JobState state() const;
    bool has_selection() const;
    int16_t selected_index() const;
    const FileEntry* selected_entry() const;
    bool handle_event(JobEvent event, int16_t selected_index = -1);
    bool can_run() const;

private:
    int16_t selected_index_ = -1;
    JobState state_ = JobState::NoFileSelected;
};
