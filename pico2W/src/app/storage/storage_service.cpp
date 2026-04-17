#include "app/storage/storage_service.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "ff.h"
}

#include "pico/stdlib.h"

namespace {
FATFS g_sd_fs;
constexpr uint32_t kRetryIntervalMs = 1000;
constexpr uint32_t kHealthCheckIntervalMs = 500;
constexpr uint8_t kHealthFailureThreshold = 2;
constexpr std::size_t kSectorSize = 512;

bool has_extension(const char* name, const char* extension) {
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }

    while (*dot != '\0' && *extension != '\0') {
        char lhs = *dot++;
        char rhs = *extension++;
        if (lhs >= 'a' && lhs <= 'z') {
            lhs = static_cast<char>(lhs - ('a' - 'A'));
        }
        if (rhs >= 'a' && rhs <= 'z') {
            rhs = static_cast<char>(rhs - ('a' - 'A'));
        }
        if (lhs != rhs) {
            return false;
        }
    }

    return *dot == '\0' && *extension == '\0';
}

bool is_supported_job_file(const char* name) {
    return has_extension(name, ".NC") ||
           has_extension(name, ".GCODE") ||
           has_extension(name, ".TAP") ||
           has_extension(name, ".NGC") ||
           has_extension(name, ".GC");
}

void copy_text(char* dest, std::size_t size, const char* src) {
    if (dest == nullptr || size == 0) {
        return;
    }

    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }

    std::snprintf(dest, size, "%s", src);
}

void fill_entry(FileEntry& entry, const FILINFO& info) {
    copy_text(entry.name, sizeof(entry.name), info.fname);
    copy_text(entry.summary, sizeof(entry.summary), "STORAGE FILE");
    copy_text(entry.tool_text, sizeof(entry.tool_text), "--");
    copy_text(entry.zero_text, sizeof(entry.zero_text), "--");

    if (info.fsize < 1024) {
        std::snprintf(entry.size_text, sizeof(entry.size_text), "%lu B", static_cast<unsigned long>(info.fsize));
    } else {
        std::snprintf(entry.size_text,
                      sizeof(entry.size_text),
                      "%lu KB",
                      static_cast<unsigned long>((info.fsize + 1023u) / 1024u));
    }
}
}  // namespace

StorageService::StorageService(SdSpiCard& sd_card) : sd_card_(sd_card) {}

void StorageService::initialize(JobStateMachine& jobs) {
    jobs.clear_files();
    state_ = StorageState::Mounting;
    health_failure_count_ = 0;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    last_attempt_ms_ = (now > kRetryIntervalMs) ? (now - kRetryIntervalMs) : 0;
    last_health_check_ms_ = now;
}

bool StorageService::poll(JobStateMachine& jobs) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    if (state_ == StorageState::Mounted) {
        if ((now - last_health_check_ms_) < kHealthCheckIntervalMs) {
            return false;
        }
        last_health_check_ms_ = now;
        return check_health(jobs);
    }

    if ((now - last_attempt_ms_) < kRetryIntervalMs) {
        return false;
    }

    return try_mount(jobs);
}

bool StorageService::is_mounted() const {
    return state_ == StorageState::Mounted;
}

StorageState StorageService::state() const {
    return state_;
}

const char* StorageService::status_text() const {
    switch (state_) {
        case StorageState::Mounted:
            return "OK";
        case StorageState::Mounting:
            return "MNT";
        case StorageState::MountError:
            return "ERR";
        case StorageState::ScanError:
            return "SCAN";
        case StorageState::Uninitialized:
        default:
            return "--";
    }
}

bool StorageService::force_remount(JobStateMachine& jobs) {
    f_unmount("0:");
    state_ = StorageState::Mounting;
    health_failure_count_ = 0;
    last_attempt_ms_ = 0;
    last_health_check_ms_ = to_ms_since_boot(get_absolute_time());
    return try_mount(jobs);
}

bool StorageService::refresh_job_files(JobStateMachine& jobs) {
    char loaded_name[sizeof(FileEntry{}.name)]{};
    const FileEntry* loaded_entry = jobs.loaded_entry();
    const JobState previous_state = jobs.state();
    if (loaded_entry != nullptr) {
        copy_text(loaded_name, sizeof(loaded_name), loaded_entry->name);
    }

    jobs.clear_files();
    if (!is_mounted()) {
        return false;
    }

    DIR dir{};
    FILINFO info{};
    FRESULT result = f_opendir(&dir, "0:/");
    int16_t restored_loaded_index = -1;
    if (result != FR_OK) {
        state_ = StorageState::ScanError;
        return false;
    }

    while (jobs.count() < JobStateMachine::kMaxFiles) {
        result = f_readdir(&dir, &info);
        if (result != FR_OK) {
            f_closedir(&dir);
            state_ = StorageState::ScanError;
            jobs.clear_files();
            return false;
        }
        if (info.fname[0] == '\0') {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0 || !is_supported_job_file(info.fname)) {
            continue;
        }

        FileEntry entry{};
        fill_entry(entry, info);
        jobs.add_file(entry);
        if (loaded_name[0] != '\0' && std::strcmp(entry.name, loaded_name) == 0) {
            restored_loaded_index = static_cast<int16_t>(jobs.count() - 1);
        }
    }

    f_closedir(&dir);
    if (restored_loaded_index >= 0) {
        jobs.handle_event(JobEvent::LoadFile, restored_loaded_index);
        if (previous_state == JobState::Running) {
            jobs.handle_event(JobEvent::StartRun);
        }
    }
    state_ = StorageState::Mounted;
    return true;
}

uint64_t StorageService::free_bytes() const {
    if (state_ != StorageState::Mounted) {
        return 0;
    }
    FATFS* fs;
    DWORD free_clust = 0;
    if (f_getfree("0:", &free_clust, &fs) != FR_OK) {
        return 0;
    }
    return static_cast<uint64_t>(free_clust) * fs->csize * 512u;
}

bool StorageService::try_mount(JobStateMachine& jobs) {
    const StorageState previous_state = state_;
    const std::size_t previous_count = jobs.count();
    last_attempt_ms_ = to_ms_since_boot(get_absolute_time());
    state_ = StorageState::Mounting;
    health_failure_count_ = 0;

    if (!sd_card_.initialize_card()) {
        state_ = StorageState::MountError;
        jobs.clear_files();
        return previous_state != state_ || previous_count != jobs.count();
    }

    f_unmount("0:");
    const FRESULT mount_result = f_mount(&g_sd_fs, "0:", 1);
    if (mount_result != FR_OK) {
        state_ = StorageState::MountError;
        jobs.clear_files();
        return previous_state != state_ || previous_count != jobs.count();
    }

    state_ = StorageState::Mounted;
    const bool files_loaded = refresh_job_files(jobs);
    last_health_check_ms_ = last_attempt_ms_;
    return previous_state != state_ || previous_count != jobs.count() || files_loaded;
}

bool StorageService::check_health(JobStateMachine& jobs) {
    uint8_t sector_buffer[kSectorSize]{};
    if (sd_card_.read_blocks(0, sector_buffer, 1)) {
        health_failure_count_ = 0;
        return false;
    }

    ++health_failure_count_;
    if (health_failure_count_ < kHealthFailureThreshold) {
        return false;
    }

    f_unmount("0:");
    state_ = StorageState::ScanError;
    jobs.clear_files();
    health_failure_count_ = 0;
    return true;
}
