#pragma once

#include <cstdint>

enum class MotionCommandType : uint8_t {
    None,
    JobStart,
    Hold,
    Resume,
    Abort,
    Jog,
    HomeAll,
    ZeroAll,
    TelemetryProbe,
};

struct MotionLinkSnapshot {
    bool linked = false;
    bool command_in_flight = false;
    bool telemetry_pending = false;
    bool urgent_pending = false;
    MotionCommandType active_command = MotionCommandType::None;
};
