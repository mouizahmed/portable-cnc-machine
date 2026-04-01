#pragma once

#include <cstdint>

#include "drivers/xpt2046.h"
#include "ui/components/ui_shell_types.h"

enum class UiEventType : uint8_t {
    TouchPressed,
    TouchReleased,
};

struct UiEvent {
    UiEventType type = UiEventType::TouchPressed;
    TouchPoint touch{};
};

struct UiEventResult {
    bool handled = false;
    bool has_navigation = false;
    NavTab navigation_target = NavTab::Home;
    bool refresh_status_bar = false;
    bool refresh_screen = false;
};
