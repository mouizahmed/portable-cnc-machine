#include "ui/components/app_frame.h"

#include <cstdio>

#include "ui/layout/ui_layout.h"

AppFrame::AppFrame(Ili9488& display)
    : display_(display),
      status_bar_(display),
      nav_bar_(display) {}

void AppFrame::render_chrome(const StatusSnapshot& status, NavTab active_tab) const {
    display_.fill_screen(COLOR_BG);
    status_bar_.render(status);
    nav_bar_.render(active_tab);
}

void AppFrame::render_status_bar(const StatusSnapshot& status) const {
    status_bar_.render(status);
}

void AppFrame::draw_footer_status(const char* status_text) const {
    char line[48];
    std::snprintf(line, sizeof(line), "STATUS: %s", status_text);
    display_.draw_text(UiLayout::kFooterStatusX, UiLayout::kFooterStatusY, line, COLOR_TEXT, COLOR_BG, 1);
}

UiEventResult AppFrame::handle_event(const UiEvent& event) const {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    NavTab selected_tab = NavTab::Home;
    if (!nav_bar_.hit_test(event.touch, selected_tab)) {
        return UiEventResult{};
    }

    return UiEventResult{true, true, selected_tab};
}

int16_t AppFrame::content_top() const {
    return static_cast<int16_t>(UiLayout::kTopBarHeight + 8);
}

int16_t AppFrame::content_bottom() const {
    return static_cast<int16_t>(LCD_HEIGHT - UiLayout::kBottomBarHeight - 1);
}
