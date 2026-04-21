#include "app/settings/machine_settings_store.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
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
constexpr uint32_t kMachineSettingsMagic = 0x4D534731;
constexpr uint16_t kMachineSettingsVersion = 1;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - (3 * FLASH_SECTOR_SIZE);

struct PersistedMachineSettings {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    float steps_per_mm_x;
    float steps_per_mm_y;
    float steps_per_mm_z;
    float max_feed_rate_x;
    float max_feed_rate_y;
    float max_feed_rate_z;
    float acceleration_x;
    float acceleration_y;
    float acceleration_z;
    float max_travel_x;
    float max_travel_y;
    float max_travel_z;
    uint8_t soft_limits_enabled;
    uint8_t hard_limits_enabled;
    uint8_t valid;
    uint8_t reserved_flags;
    float spindle_min_rpm;
    float spindle_max_rpm;
    float warning_temperature;
    float max_temperature;
    uint32_t checksum;
};

alignas(FLASH_PAGE_SIZE) static uint8_t flash_sector_buffer[FLASH_SECTOR_SIZE];

uint32_t machine_settings_checksum(const PersistedMachineSettings& settings) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&settings);
    uint32_t checksum = 0x39A4F10Du;
    for (std::size_t i = 0; i < offsetof(PersistedMachineSettings, checksum); ++i) {
        checksum = (checksum * 33u) ^ bytes[i];
    }
    return checksum;
}

PersistedMachineSettings to_persisted(const MachineSettings& settings) {
    PersistedMachineSettings stored{};
    stored.magic = kMachineSettingsMagic;
    stored.version = kMachineSettingsVersion;
    stored.steps_per_mm_x = settings.steps_per_mm_x;
    stored.steps_per_mm_y = settings.steps_per_mm_y;
    stored.steps_per_mm_z = settings.steps_per_mm_z;
    stored.max_feed_rate_x = settings.max_feed_rate_x;
    stored.max_feed_rate_y = settings.max_feed_rate_y;
    stored.max_feed_rate_z = settings.max_feed_rate_z;
    stored.acceleration_x = settings.acceleration_x;
    stored.acceleration_y = settings.acceleration_y;
    stored.acceleration_z = settings.acceleration_z;
    stored.max_travel_x = settings.max_travel_x;
    stored.max_travel_y = settings.max_travel_y;
    stored.max_travel_z = settings.max_travel_z;
    stored.soft_limits_enabled = settings.soft_limits_enabled ? 1 : 0;
    stored.hard_limits_enabled = settings.hard_limits_enabled ? 1 : 0;
    stored.valid = 1;
    stored.spindle_min_rpm = settings.spindle_min_rpm;
    stored.spindle_max_rpm = settings.spindle_max_rpm;
    stored.warning_temperature = settings.warning_temperature;
    stored.max_temperature = settings.max_temperature;
    stored.checksum = machine_settings_checksum(stored);
    return stored;
}

MachineSettings from_persisted(const PersistedMachineSettings& stored) {
    return MachineSettings{
        stored.steps_per_mm_x,
        stored.steps_per_mm_y,
        stored.steps_per_mm_z,
        stored.max_feed_rate_x,
        stored.max_feed_rate_y,
        stored.max_feed_rate_z,
        stored.acceleration_x,
        stored.acceleration_y,
        stored.acceleration_z,
        stored.max_travel_x,
        stored.max_travel_y,
        stored.max_travel_z,
        stored.soft_limits_enabled != 0,
        stored.hard_limits_enabled != 0,
        stored.spindle_min_rpm,
        stored.spindle_max_rpm,
        stored.warning_temperature,
        stored.max_temperature
    };
}

void assert_storage_region_reserved() {
    const uintptr_t flash_binary_end =
        reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    hard_assert(flash_binary_end <= kFlashOffset);
}

} // namespace

MachineSettingsStore::MachineSettingsStore()
    : current_(defaults()) {
    MachineSettings stored{};
    if (load_from_flash(stored)) {
        current_ = stored;
    }
    revision_ = 1;
}

MachineSettings MachineSettingsStore::defaults() {
    return MachineSettings{};
}

const MachineSettings& MachineSettingsStore::current() const {
    return current_;
}

uint32_t MachineSettingsStore::revision() const {
    return revision_;
}

bool MachineSettingsStore::apply(const MachineSettings& candidate, const char** error_reason) {
    const char* reason = validate(candidate);
    if (error_reason != nullptr) {
        *error_reason = reason;
    }
    if (reason != nullptr) {
        return false;
    }

    if (!save_to_flash(candidate)) {
        if (error_reason != nullptr) {
            *error_reason = "SETTINGS_ERR_PERSIST";
        }
        return false;
    }

    current_ = candidate;
    ++revision_;
    return true;
}

bool MachineSettingsStore::in_range(float value, float min, float max) {
    return std::isfinite(value) && value >= min && value <= max;
}

const char* MachineSettingsStore::validate(const MachineSettings& candidate) {
    if (!in_range(candidate.steps_per_mm_x, kMinStepsPerMm, kMaxStepsPerMm)) return "SETTINGS_ERR_STEPS_PER_MM_X";
    if (!in_range(candidate.steps_per_mm_y, kMinStepsPerMm, kMaxStepsPerMm)) return "SETTINGS_ERR_STEPS_PER_MM_Y";
    if (!in_range(candidate.steps_per_mm_z, kMinStepsPerMm, kMaxStepsPerMm)) return "SETTINGS_ERR_STEPS_PER_MM_Z";

    if (!in_range(candidate.max_feed_rate_x, kMinFeedRate, kMaxFeedRate)) return "SETTINGS_ERR_MAX_FEED_RATE_X";
    if (!in_range(candidate.max_feed_rate_y, kMinFeedRate, kMaxFeedRate)) return "SETTINGS_ERR_MAX_FEED_RATE_Y";
    if (!in_range(candidate.max_feed_rate_z, kMinFeedRate, kMaxFeedRate)) return "SETTINGS_ERR_MAX_FEED_RATE_Z";

    if (!in_range(candidate.acceleration_x, kMinAcceleration, kMaxAcceleration)) return "SETTINGS_ERR_ACCELERATION_X";
    if (!in_range(candidate.acceleration_y, kMinAcceleration, kMaxAcceleration)) return "SETTINGS_ERR_ACCELERATION_Y";
    if (!in_range(candidate.acceleration_z, kMinAcceleration, kMaxAcceleration)) return "SETTINGS_ERR_ACCELERATION_Z";

    if (!in_range(candidate.max_travel_x, kMinTravel, kMaxTravel)) return "SETTINGS_ERR_MAX_TRAVEL_X";
    if (!in_range(candidate.max_travel_y, kMinTravel, kMaxTravel)) return "SETTINGS_ERR_MAX_TRAVEL_Y";
    if (!in_range(candidate.max_travel_z, kMinTravel, kMaxTravel)) return "SETTINGS_ERR_MAX_TRAVEL_Z";

    if (!in_range(candidate.spindle_min_rpm, kMinSpindleRpm, kMaxSpindleRpm)) return "SETTINGS_ERR_SPINDLE_MIN_RPM";
    if (!in_range(candidate.spindle_max_rpm, kMinSpindleRpm, kMaxSpindleRpm)) return "SETTINGS_ERR_SPINDLE_MAX_RPM";
    if (candidate.spindle_min_rpm > candidate.spindle_max_rpm) return "SETTINGS_ERR_SPINDLE_RANGE";

    if (!in_range(candidate.warning_temperature, kMinTemperature, kMaxTemperatureLimit)) return "SETTINGS_ERR_WARNING_TEMPERATURE";
    if (!in_range(candidate.max_temperature, kMinTemperature, kMaxTemperatureLimit)) return "SETTINGS_ERR_MAX_TEMPERATURE";
    if (candidate.warning_temperature > candidate.max_temperature) return "SETTINGS_ERR_TEMPERATURE_RANGE";

    return nullptr;
}

bool MachineSettingsStore::load_from_flash(MachineSettings& settings) const {
    assert_storage_region_reserved();

    const auto* stored =
        reinterpret_cast<const PersistedMachineSettings*>(XIP_BASE + kFlashOffset);
    if (stored->magic != kMachineSettingsMagic ||
        stored->version != kMachineSettingsVersion ||
        stored->valid != 1) {
        return false;
    }
    if (stored->checksum != machine_settings_checksum(*stored)) {
        return false;
    }

    const MachineSettings candidate = from_persisted(*stored);
    if (validate(candidate) != nullptr) {
        return false;
    }

    settings = candidate;
    return true;
}

bool MachineSettingsStore::save_to_flash(const MachineSettings& settings) const {
    assert_storage_region_reserved();

    const PersistedMachineSettings stored = to_persisted(settings);

    std::memset(flash_sector_buffer, 0xFF, sizeof(flash_sector_buffer));
    std::memcpy(flash_sector_buffer, &stored, sizeof(stored));

    // Leave the shared SPI peripherals deselected before flash erase/program.
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

    MachineSettings verify{};
    return load_from_flash(verify) && validate(verify) == nullptr;
}
