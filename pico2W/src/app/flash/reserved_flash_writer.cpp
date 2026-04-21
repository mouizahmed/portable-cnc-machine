#include "app/flash/reserved_flash_writer.h"

#include <cstring>

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/assert.h"
#include "pico/multicore.h"

extern "C" char __flash_binary_end;

namespace {
alignas(FLASH_PAGE_SIZE) uint8_t flash_sector_buffer[FLASH_SECTOR_SIZE];
constexpr uint64_t kCore1LockoutTimeoutUs = 250000;
}

void assert_reserved_flash_region(uint32_t offset, std::size_t length) {
    hard_assert((offset % FLASH_SECTOR_SIZE) == 0u);
    hard_assert(length <= FLASH_SECTOR_SIZE);
    hard_assert((offset + FLASH_SECTOR_SIZE) <= PICO_FLASH_SIZE_BYTES);

    const uintptr_t flash_binary_end =
        reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    hard_assert(flash_binary_end <= offset);
}

bool write_reserved_flash_sector(uint32_t offset, const uint8_t* data, std::size_t length) {
    assert_reserved_flash_region(offset, length);

    std::memset(flash_sector_buffer, 0xFF, sizeof(flash_sector_buffer));
    if (data != nullptr && length > 0) {
        std::memcpy(flash_sector_buffer, data, length);
    }

    // Leave shared SPI peripherals deselected before flash erase/program.
    gpio_put(PIN_LCD_CS, 1);
    gpio_put(PIN_TOUCH_CS, 1);
    gpio_put(PIN_SD_CS, 1);

    const bool lockout_core1 = multicore_lockout_victim_is_initialized(1);
    if (lockout_core1) {
        if (!multicore_lockout_start_timeout_us(kCore1LockoutTimeoutUs)) {
            return false;
        }
    }

    const uint32_t interrupt_state = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, flash_sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupt_state);

    if (lockout_core1) {
        if (!multicore_lockout_end_timeout_us(kCore1LockoutTimeoutUs)) {
            return false;
        }
    }

    if (data == nullptr || length == 0) {
        return true;
    }

    const auto* written = reinterpret_cast<const uint8_t*>(XIP_BASE + offset);
    return std::memcmp(written, data, length) == 0;
}
