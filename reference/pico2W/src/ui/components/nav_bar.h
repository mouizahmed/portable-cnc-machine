#pragma once

#include <array>

#include "drivers/ili9488.h"
#include "ui/components/ui_shell_types.h"
#include "ui/helpers/ui_helpers.h"

class NavBar {
public:
    explicit NavBar(Ili9488& display);

    void render(NavTab active_tab) const;
    bool hit_test(const TouchPoint& point, NavTab& selected_tab) const;

private:
    struct NavButton {
        NavTab tab;
        const char* label;
        UiRect rect;
    };

    static const std::array<NavButton, 5> kNavButtons;

    Ili9488& display_;

    void draw_button(const NavButton& button, bool active) const;
};
