#include "app_frame.h"

#include <cstdio>

#include "config.h"

namespace {
constexpr int16_t kTopBarHeight = 22;
constexpr int16_t kBottomBarHeight = 30;
constexpr int16_t kTitleY = 34;
constexpr int16_t kSubtitleY = 64;

constexpr uint16_t kColorPanel = rgb565(28, 36, 46);
constexpr uint16_t kColorNavIdle = rgb565(36, 46, 58);
constexpr uint16_t kColorNavActive = rgb565(46, 92, 168);
}  // namespace

const std::array<AppFrame::NavButton, 5> AppFrame::kNavButtons{{
    {NavTab::Home, "HOME"},
    {NavTab::Jog, "JOG"},
    {NavTab::Files, "FILES"},
    {NavTab::Status, "STATUS"},
    {NavTab::Settings, "SETTINGS"},
}};

AppFrame::AppFrame(Ili9488& display) : display_(display) {}

void AppFrame::render_chrome(const StatusSnapshot& status, NavTab active_tab) const {
    display_.fill_screen(COLOR_BG);
    draw_top_bar(status);
    draw_bottom_nav(active_tab);
}

void AppFrame::draw_screen_title(const char* title, const char* subtitle) const {
    draw_centered_text(kTitleY, title, COLOR_TEXT, COLOR_BG, 3);
    draw_centered_text(kSubtitleY, subtitle, COLOR_MUTED, COLOR_BG, 1);
}

void AppFrame::draw_footer_status(const char* status_text) const {
    char line[48];
    std::snprintf(line, sizeof(line), "STATUS: %s", status_text);
    display_.draw_text(24, 262, line, COLOR_TEXT, COLOR_BG, 1);
}

int16_t AppFrame::content_top() const {
    return static_cast<int16_t>(kTopBarHeight + 8);
}

int16_t AppFrame::content_bottom() const {
    return static_cast<int16_t>(LCD_HEIGHT - kBottomBarHeight - 1);
}

void AppFrame::draw_top_bar(const StatusSnapshot& status) const {
    char line[96];

    display_.fill_rect(0, 0, LCD_WIDTH, kTopBarHeight, COLOR_HEADER);
    display_.draw_rect(0, 0, LCD_WIDTH, kTopBarHeight, COLOR_BORDER);

    std::snprintf(line,
                  sizeof(line),
                  "ESTOP: %s | WIFI: %s | SD: %s | USB: %s | TOOL: %s | XYZ: %s | %s",
                  status.estop,
                  status.wifi,
                  status.sd,
                  status.usb,
                  status.tool,
                  status.xyz,
                  status.time_text);
    display_.draw_text(6, 7, line, COLOR_TEXT, COLOR_HEADER, 1);
}

void AppFrame::draw_bottom_nav(NavTab active_tab) const {
    display_.fill_rect(0, LCD_HEIGHT - kBottomBarHeight, LCD_WIDTH, kBottomBarHeight, COLOR_HEADER);
    display_.draw_rect(0, LCD_HEIGHT - kBottomBarHeight, LCD_WIDTH, kBottomBarHeight, COLOR_BORDER);

    const int16_t button_width = 84;
    const int16_t gap = 10;
    const int16_t start_x = 10;

    for (std::size_t i = 0; i < kNavButtons.size(); ++i) {
        const int16_t x = static_cast<int16_t>(start_x + i * (button_width + gap));
        draw_nav_button(x, button_width, kNavButtons[i], kNavButtons[i].tab == active_tab);
    }
}

void AppFrame::draw_nav_button(int16_t x, int16_t width, const NavButton& button, bool active) const {
    const int16_t y = static_cast<int16_t>(LCD_HEIGHT - kBottomBarHeight + 4);
    const int16_t height = static_cast<int16_t>(kBottomBarHeight - 8);
    const uint16_t fill = active ? kColorNavActive : kColorNavIdle;

    display_.fill_rect(x, y, width, height, fill);
    display_.draw_rect(x, y, width, height, COLOR_BORDER);

    const int16_t text_x = static_cast<int16_t>(x + (width - display_.text_width(button.label, 1)) / 2);
    const int16_t text_y = static_cast<int16_t>(y + (height - display_.text_height(1)) / 2);
    display_.draw_text(text_x, text_y, button.label, COLOR_TEXT, fill, 1);
}

void AppFrame::draw_centered_text(int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) const {
    const int16_t width = display_.text_width(text, scale);
    const int16_t x = static_cast<int16_t>((LCD_WIDTH - width) / 2);
    display_.draw_text(x, y, text, fg, bg, scale);
}
