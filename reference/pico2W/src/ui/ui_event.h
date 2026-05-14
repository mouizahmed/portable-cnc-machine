#pragma once

#include <cstdint>

#include "app/jog/jog_state_machine.h"
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

enum class UiCommandType : uint8_t {
    None,
    SelectFile,
    StartJob,
    HoldJob,
    ResumeJob,
    JogMove,
    HomeAll,
    ZeroAll,
};

struct UiEventResult {
    bool handled = false;
    bool has_navigation = false;
    NavTab navigation_target = NavTab::Home;
    bool refresh_status_bar = false;
    bool refresh_screen = false;
    UiCommandType command = UiCommandType::None;
    int16_t selected_index = -1;
    JogAction jog_action = JogAction::MoveXPositive;
};
