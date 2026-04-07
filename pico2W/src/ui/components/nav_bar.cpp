#include "ui/components/nav_bar.h"

#include "config.h"
#include "ui/layout/ui_layout.h"

namespace {
constexpr uint16_t kColorNavIdle = rgb565(36, 46, 58);
constexpr uint16_t kColorNavActive = rgb565(46, 92, 168);
}  // namespace

const std::array<NavBar::NavButton, 5> NavBar::kNavButtons{{
    {NavTab::Home, "HOME", UiLayout::nav_button_rect(0)},
    {NavTab::Jog, "JOG", UiLayout::nav_button_rect(1)},
    {NavTab::Files, "FILES", UiLayout::nav_button_rect(2)},
    {NavTab::Status, "STATUS", UiLayout::nav_button_rect(3)},
    {NavTab::Settings, "SETTINGS", UiLayout::nav_button_rect(4)},
}};

NavBar::NavBar(Ili9488& display) : display_(display) {}

void NavBar::render(NavTab active_tab) const {
    display_.fill_rect(0, LCD_HEIGHT - UiLayout::kBottomBarHeight, LCD_WIDTH, UiLayout::kBottomBarHeight, COLOR_HEADER);
    display_.draw_rect(0, LCD_HEIGHT - UiLayout::kBottomBarHeight, LCD_WIDTH, UiLayout::kBottomBarHeight, COLOR_BORDER);

    for (const NavButton& button : kNavButtons) {
        draw_button(button, button.tab == active_tab);
    }
}

bool NavBar::hit_test(const TouchPoint& point, NavTab& selected_tab) const {
    for (const NavButton& button : kNavButtons) {
        if (ui_rect_contains(button.rect, point)) {
            selected_tab = button.tab;
            return true;
        }
    }

    return false;
}

void NavBar::draw_button(const NavButton& button, bool active) const {
    const uint16_t fill = active ? kColorNavActive : kColorNavIdle;
    const uint8_t scale = 1;

    display_.fill_rect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, fill);
    display_.draw_rect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, COLOR_BORDER);

    const int16_t text_x = static_cast<int16_t>(button.rect.x + (button.rect.w - display_.text_width(button.label, scale)) / 2);
    const int16_t text_y = static_cast<int16_t>(button.rect.y + (button.rect.h - display_.text_height(scale)) / 2);
    display_.draw_text(text_x, text_y, button.label, COLOR_TEXT, fill, scale);
}
