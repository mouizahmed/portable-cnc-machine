#include "ui/components/status_bar.h"

#include "config.h"
#include "ui/layout/ui_layout.h"

StatusBar::StatusBar(Ili9488& display) : display_(display) {}

void StatusBar::render(const StatusSnapshot& status) const {
    char line[96];

    display_.fill_rect(0, 0, LCD_WIDTH, UiLayout::kTopBarHeight, COLOR_HEADER);
    display_.draw_rect(0, 0, LCD_WIDTH, UiLayout::kTopBarHeight, COLOR_BORDER);

    formatter_.format_top_bar(status, line, sizeof(line));
    display_.draw_text(6, 7, line, COLOR_TEXT, COLOR_HEADER, 1);
}
