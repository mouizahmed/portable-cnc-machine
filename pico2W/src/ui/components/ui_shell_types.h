#pragma once

#include <cstdint>

enum class NavTab : uint8_t {
    Home,
    Jog,
    Files,
    Status,
    Settings,
};

struct StatusSnapshot {
    const char* machine;
    const char* sd;
    const char* usb;
    const char* tool;
    const char* xyz;
    const char* time_text;
};
