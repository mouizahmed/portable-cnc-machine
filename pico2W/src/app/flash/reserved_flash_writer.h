#pragma once

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

void assert_reserved_flash_region(uint32_t offset, std::size_t length = FLASH_SECTOR_SIZE);
inline void assert_reserved_flash_region(uint32_t offset, std::size_t length) {
    hard_assert((offset % FLASH_SECTOR_SIZE) == 0u);
    hard_assert(length <= FLASH_SECTOR_SIZE);
    hard_assert((offset + FLASH_SECTOR_SIZE) <= PICO_FLASH_SIZE_BYTES);

    const uintptr_t flash_binary_end =
        reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    hard_assert(flash_binary_end <= offset);
}

inline bool write_reserved_flash_sector(uint32_t offset,
                                        const uint8_t* data,
                                        std::size_t length,
                                        uint8_t* sector_buffer,
                                        std::size_t sector_buffer_size = FLASH_SECTOR_SIZE) {
    constexpr uint64_t kCore1LockoutTimeoutUs = 250000;

    assert_reserved_flash_region(offset, length);
    hard_assert(sector_buffer != nullptr);
    hard_assert(sector_buffer_size >= FLASH_SECTOR_SIZE);

    std::memset(sector_buffer, 0xFF, FLASH_SECTOR_SIZE);
    if (data != nullptr && length > 0) {
        std::memcpy(sector_buffer, data, length);
    }

    gpio_put(PIN_LCD_CS, 1);
    gpio_put(PIN_TOUCH_CS, 1);
    gpio_put(PIN_SD_CS, 1);

    const bool lockout_core1 = multicore_lockout_victim_is_initialized(1);
    if (lockout_core1 && !multicore_lockout_start_timeout_us(kCore1LockoutTimeoutUs)) {
        return false;
    }

    const uint32_t interrupt_state = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupt_state);

    if (lockout_core1 && !multicore_lockout_end_timeout_us(kCore1LockoutTimeoutUs)) {
        return false;
    }

    if (data == nullptr || length == 0) {
        return true;
    }

    const auto* written = reinterpret_cast<const uint8_t*>(XIP_BASE + offset);
    return std::memcmp(written, data, length) == 0;
}
