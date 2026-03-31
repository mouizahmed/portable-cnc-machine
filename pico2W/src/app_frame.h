#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ili9488.h"

enum class NavTab : uint8_t {
    Home,
    Jog,
    Files,
    Status,
    Settings,
};

struct StatusSnapshot {
    const char* estop;
    const char* wifi;
    const char* sd;
    const char* usb;
    const char* tool;
    const char* xyz;
    const char* time_text;
};

class AppFrame {
public:
    explicit AppFrame(Ili9488& display);

    void render_chrome(const StatusSnapshot& status, NavTab active_tab) const;
    void draw_screen_title(const char* title, const char* subtitle) const;
    void draw_footer_status(const char* status_text) const;

    int16_t content_top() const;
    int16_t content_bottom() const;

private:
    struct NavButton {
        NavTab tab;
        const char* label;
    };

    static const std::array<NavButton, 5> kNavButtons;

    Ili9488& display_;

    void draw_top_bar(const StatusSnapshot& status) const;
    void draw_bottom_nav(NavTab active_tab) const;
    void draw_nav_button(int16_t x, int16_t width, const NavButton& button, bool active) const;
    void draw_centered_text(int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) const;
};
