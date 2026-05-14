#pragma once

#include <array>
#include <cstddef>

#include "ui/components/ui_shell_types.h"
#include "ui/screens/screen.h"

class ScreenRouter {
public:
    void register_screen(Screen& screen);
    bool can_navigate_to(NavTab tab) const;
    bool navigate_to(NavTab tab);
    Screen& current();
    const Screen& current() const;

private:
    static constexpr std::size_t kTabCount = 5;

    std::array<Screen*, kTabCount> screens_{};
    NavTab current_tab_ = NavTab::Home;

    static constexpr std::size_t to_index(NavTab tab) {
        return static_cast<std::size_t>(tab);
    }
};
