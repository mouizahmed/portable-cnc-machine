#include "app/job/loaded_job_storage.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app/job/job_state_machine.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/assert.h"

extern "C" char __flash_binary_end;

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

void write_sector(const void* data, std::size_t size) {
    std::memset(flash_sector_buffer, 0xFF, sizeof(flash_sector_buffer));
    if (data != nullptr && size > 0) {
        std::memcpy(flash_sector_buffer, data, size);
    }

    const uint32_t interrupt_state = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, flash_sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupt_state);
}

void assert_storage_region_reserved() {
    const uintptr_t flash_binary_end =
        reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    hard_assert(flash_binary_end <= kFlashOffset);
}
}  // namespace

bool LoadedJobStorage::load(char* name, std::size_t size) const {
    assert_storage_region_reserved();

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
    assert_storage_region_reserved();

    if (name == nullptr || name[0] == '\0') {
        return clear();
    }

    PersistedLoadedJob stored{};
    stored.magic = kLoadedJobMagic;
    stored.version = kLoadedJobVersion;
    std::snprintf(stored.name, sizeof(stored.name), "%s", name);
    stored.valid = 1;
    stored.checksum = loaded_job_checksum(stored);

    write_sector(&stored, sizeof(stored));

    char verify[sizeof(stored.name)]{};
    return load(verify, sizeof(verify)) && std::strcmp(verify, stored.name) == 0;
}

bool LoadedJobStorage::clear() const {
    assert_storage_region_reserved();

    write_sector(nullptr, 0);

    char verify[sizeof(FileEntry{}.name)]{};
    return !load(verify, sizeof(verify));
}
