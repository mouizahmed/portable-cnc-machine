#include "calibration/calibration_storage.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/assert.h"
#include "pico/multicore.h"

extern "C" char __flash_binary_end;

namespace {
constexpr uint32_t kCalibrationMagic = 0x43414C31;
constexpr uint16_t kCalibrationVersion = 1;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct PersistedCalibration {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    uint8_t swap_xy;
    uint8_t invert_x;
    uint8_t invert_y;
    uint8_t valid;
    uint32_t checksum;
};

alignas(FLASH_PAGE_SIZE) static uint8_t flash_sector_buffer[FLASH_SECTOR_SIZE];

uint32_t calibration_checksum(const PersistedCalibration& calibration) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&calibration);
    uint32_t checksum = 0x51A7C0DE;
    for (std::size_t i = 0; i < offsetof(PersistedCalibration, checksum); ++i) {
        checksum = (checksum * 33u) ^ bytes[i];
    }
    return checksum;
}

PersistedCalibration to_persisted(const TouchCalibration& calibration) {
    PersistedCalibration stored{};
    stored.magic = kCalibrationMagic;
    stored.version = kCalibrationVersion;
    stored.x_min = calibration.x_min;
    stored.x_max = calibration.x_max;
    stored.y_min = calibration.y_min;
    stored.y_max = calibration.y_max;
    stored.swap_xy = calibration.swap_xy ? 1 : 0;
    stored.invert_x = calibration.invert_x ? 1 : 0;
    stored.invert_y = calibration.invert_y ? 1 : 0;
    stored.valid = 1;
    stored.checksum = calibration_checksum(stored);
    return stored;
}

TouchCalibration from_persisted(const PersistedCalibration& stored) {
    return TouchCalibration{
        stored.x_min,
        stored.x_max,
        stored.y_min,
        stored.y_max,
        stored.swap_xy != 0,
        stored.invert_x != 0,
        stored.invert_y != 0,
    };
}

void assert_storage_region_reserved() {
    const uintptr_t flash_binary_end =
        reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    hard_assert(flash_binary_end <= kFlashOffset);
}
}  // namespace

bool CalibrationStorage::load(TouchCalibration& calibration) const {
    assert_storage_region_reserved();

    const auto* stored = reinterpret_cast<const PersistedCalibration*>(XIP_BASE + kFlashOffset);
    if (stored->magic != kCalibrationMagic || stored->version != kCalibrationVersion || stored->valid != 1) {
        return false;
    }
    if (stored->checksum != calibration_checksum(*stored)) {
        return false;
    }

    calibration = from_persisted(*stored);
    return has_reasonable_ranges(calibration);
}

bool CalibrationStorage::save(const TouchCalibration& calibration) const {
    assert_storage_region_reserved();

    const PersistedCalibration stored = to_persisted(calibration);

    std::memset(flash_sector_buffer, 0xFF, sizeof(flash_sector_buffer));
    std::memcpy(flash_sector_buffer, &stored, sizeof(stored));

    // Leave shared SPI peripherals deselected before flash erase/program.
    gpio_put(PIN_LCD_CS, 1);
    gpio_put(PIN_TOUCH_CS, 1);
    gpio_put(PIN_SD_CS, 1);

    const bool lockout_core1 = multicore_lockout_victim_is_initialized(1);
    if (lockout_core1) {
        multicore_lockout_start_blocking();
    }
    const uint32_t interrupt_state = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, flash_sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupt_state);
    if (lockout_core1) {
        multicore_lockout_end_blocking();
    }

    TouchCalibration verify{};
    return load(verify);
}

bool CalibrationStorage::has_reasonable_ranges(const TouchCalibration& calibration) {
    return calibration.x_max > calibration.x_min &&
           calibration.y_max > calibration.y_min &&
           (calibration.x_max - calibration.x_min) >= 400 &&
           (calibration.y_max - calibration.y_min) >= 400;
}
