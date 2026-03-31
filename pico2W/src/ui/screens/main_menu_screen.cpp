#include "ui/screens/main_menu_screen.h"

#include "ui/layout/ui_layout.h"

const std::array<MenuCardSpec, 6> MainMenuScreen::kCards{{
    {UiLayout::main_menu_card_rect(0), "RUN JOB", "START / RESUME", NavTab::Home, false},
    {UiLayout::main_menu_card_rect(1), "JOG", "MOVE AXES", NavTab::Jog, false},
    {UiLayout::main_menu_card_rect(2), "ZERO/HOME", "WORK OFFSETS", NavTab::Home, false},
    {UiLayout::main_menu_card_rect(3), "FILES", "LOAD G-CODE", NavTab::Files, true},
    {UiLayout::main_menu_card_rect(4), "PROBE/TOOL", "TOUCH-OFF SETUP", NavTab::Home, false},
    {UiLayout::main_menu_card_rect(5), "SETTINGS", "CONFIG / CALIB", NavTab::Settings, false},
}};

MainMenuScreen::MainMenuScreen(Ili9488& display, AppFrame& frame)
    : frame_(frame), menu_card_(display) {}

NavTab MainMenuScreen::tab() const {
    return NavTab::Home;
}

void MainMenuScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Home);
    frame_.draw_screen_title("PORTABLE CNC", "MAIN MENU");

    for (const MenuCardSpec& card : kCards) {
        menu_card_.render(card);
    }

    frame_.draw_footer_status("READY");
}

UiEventResult MainMenuScreen::handle_event(const UiEvent& event) {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    for (const MenuCardSpec& card : kCards) {
        if (ui_rect_contains(card.rect, event.touch)) {
            if (!card.enabled) {
                return UiEventResult{true, false, tab()};
            }
            return UiEventResult{true, true, card.target_tab};
        }
    }

    return UiEventResult{};
}
