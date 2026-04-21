#include "ui/components/status_bar.h"

#include <cstdio>

#include "config.h"
#include "ui/layout/ui_layout.h"

StatusBar::StatusBar(Ili9488& display) : display_(display) {}

void StatusBar::render(const StatusSnapshot& status) const {
    char line_top[48];
    constexpr int16_t kTopBarTextInsetX = 12;

    display_.fill_rect(0, 0, LCD_WIDTH, UiLayout::kTopBarHeight, COLOR_HEADER);
    display_.draw_rect(0, 0, LCD_WIDTH, UiLayout::kTopBarHeight, COLOR_BORDER);

    std::snprintf(line_top, sizeof(line_top), "M:%s STR:%s USB:%s", status.machine, status.sd, status.usb);

    const int16_t text_y = static_cast<int16_t>((UiLayout::kTopBarHeight - display_.text_height(2)) / 2);
    display_.draw_text(kTopBarTextInsetX, text_y, line_top, COLOR_TEXT, COLOR_HEADER, 2);
}
