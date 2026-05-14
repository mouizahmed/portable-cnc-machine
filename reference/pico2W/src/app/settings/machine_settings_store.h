#pragma once

#include <cstdint>

struct MachineSettings {
    float steps_per_mm_x = 800.0f;
    float steps_per_mm_y = 800.0f;
    float steps_per_mm_z = 800.0f;
    float max_feed_rate_x = 5000.0f;
    float max_feed_rate_y = 5000.0f;
    float max_feed_rate_z = 1000.0f;
    float acceleration_x = 200.0f;
    float acceleration_y = 200.0f;
    float acceleration_z = 100.0f;
    float max_travel_x = 300.0f;
    float max_travel_y = 200.0f;
    float max_travel_z = 100.0f;
    bool soft_limits_enabled = true;
    bool hard_limits_enabled = true;
    float spindle_min_rpm = 1000.0f;
    float spindle_max_rpm = 24000.0f;
    float warning_temperature = 40.0f;
    float max_temperature = 50.0f;
};

class MachineSettingsStore {
public:
    MachineSettingsStore();

    static MachineSettings defaults();
    const MachineSettings& current() const;
    uint32_t revision() const;
    bool apply(const MachineSettings& candidate, const char** error_reason);

private:
    MachineSettings current_;
    uint32_t revision_ = 0;

    static constexpr float kMinStepsPerMm = 1.0f;
    static constexpr float kMaxStepsPerMm = 20000.0f;
    static constexpr float kMinFeedRate = 1.0f;
    static constexpr float kMaxFeedRate = 50000.0f;
    static constexpr float kMinAcceleration = 1.0f;
    static constexpr float kMaxAcceleration = 10000.0f;
    static constexpr float kMinTravel = 1.0f;
    static constexpr float kMaxTravel = 5000.0f;
    static constexpr float kMinSpindleRpm = 0.0f;
    static constexpr float kMaxSpindleRpm = 50000.0f;
    static constexpr float kMinTemperature = 0.0f;
    static constexpr float kMaxTemperatureLimit = 150.0f;

    static bool in_range(float value, float min, float max);
    static const char* validate(const MachineSettings& candidate);
    bool load_from_flash(MachineSettings& settings) const;
    bool save_to_flash(const MachineSettings& settings) const;
};
