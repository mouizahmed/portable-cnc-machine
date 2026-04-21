#include "app/job/loaded_job_storage.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app/flash/reserved_flash_writer.h"
#include "app/job/job_state_machine.h"
#include "hardware/flash.h"

namespace {
constexpr uint32_t kLoadedJobMagic = 0x4A4F4231;
constexpr uint16_t kLoadedJobVersion = 1;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE);

struct PersistedLoadedJob {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char name[sizeof(FileEntry{}.name)];
    uint8_t valid;
    uint8_t reserved_bytes[3];
    uint32_t checksum;
};

alignas(FLASH_PAGE_SIZE) static uint8_t flash_sector_buffer[FLASH_SECTOR_SIZE];

uint32_t loaded_job_checksum(const PersistedLoadedJob& job) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&job);
    uint32_t checksum = 0x7A11D0AD;
    for (std::size_t i = 0; i < offsetof(PersistedLoadedJob, checksum); ++i) {
        checksum = (checksum * 33u) ^ bytes[i];
    }
    return checksum;
}

}  // namespace

bool LoadedJobStorage::load(char* name, std::size_t size) const {
    assert_reserved_flash_region(kFlashOffset);

    if (name == nullptr || size == 0) {
        return false;
    }

    name[0] = '\0';

    const auto* stored =
        reinterpret_cast<const PersistedLoadedJob*>(XIP_BASE + kFlashOffset);
    if (stored->magic != kLoadedJobMagic ||
        stored->version != kLoadedJobVersion ||
        stored->valid != 1 ||
        stored->name[0] == '\0') {
        return false;
    }
    if (stored->checksum != loaded_job_checksum(*stored)) {
        return false;
    }

    std::snprintf(name, size, "%s", stored->name);
    return name[0] != '\0';
}

bool LoadedJobStorage::save(const char* name) const {
    assert_reserved_flash_region(kFlashOffset);

    if (name == nullptr || name[0] == '\0') {
        return clear();
    }

    PersistedLoadedJob stored{};
    stored.magic = kLoadedJobMagic;
    stored.version = kLoadedJobVersion;
    std::snprintf(stored.name, sizeof(stored.name), "%s", name);
    stored.valid = 1;
    stored.checksum = loaded_job_checksum(stored);

    if (!write_reserved_flash_sector(
            kFlashOffset,
            reinterpret_cast<const uint8_t*>(&stored),
            sizeof(stored),
            flash_sector_buffer,
            sizeof(flash_sector_buffer))) {
        return false;
    }

    char verify[sizeof(stored.name)]{};
    return load(verify, sizeof(verify)) && std::strcmp(verify, stored.name) == 0;
}

bool LoadedJobStorage::clear() const {
    assert_reserved_flash_region(kFlashOffset);

    if (!write_reserved_flash_sector(
            kFlashOffset,
            nullptr,
            0,
            flash_sector_buffer,
            sizeof(flash_sector_buffer))) {
        return false;
    }

    char verify[sizeof(FileEntry{}.name)]{};
    return !load(verify, sizeof(verify));
}
