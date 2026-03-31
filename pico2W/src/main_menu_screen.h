#pragma once

#include <array>
#include <cstdint>

#include "app_frame.h"

class MainMenuScreen {
public:
    MainMenuScreen(Ili9488& display, AppFrame& frame);

    void render() const;

private:
    struct MenuCard {
        int16_t x;
        int16_t y;
        int16_t w;
        int16_t h;
        const char* title;
        const char* subtitle;
    };

    static const std::array<MenuCard, 6> kCards;

    Ili9488& display_;
    AppFrame& frame_;

    void draw_card(const MenuCard& card) const;
};
