#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/state_types.h"

enum class JobEvent : uint8_t {
    SelectFile,
    StartRun,
    ClearSelection,
};

struct FileEntry {
    char name[32]{};
    char summary[24]{};
    char size_text[16]{};
    char tool_text[16]{};
    char zero_text[16]{};
};

class JobStateMachine {
public:
    static constexpr std::size_t kMaxFiles = 5;

    std::size_t count() const;
    const FileEntry& entry(std::size_t index) const;

    JobState state() const;
    bool has_selection() const;
    int16_t selected_index() const;
    const FileEntry* selected_entry() const;
    bool handle_event(JobEvent event, int16_t selected_index = -1);
    bool can_run() const;
    void clear_files();
    bool add_file(const FileEntry& entry);
    void set_state(JobState state, int16_t selected_index = -1);

private:
    std::array<FileEntry, kMaxFiles> entries_{};
    std::size_t entry_count_ = 0;
    int16_t selected_index_ = -1;
    JobState state_ = JobState::NoFileSelected;
};
