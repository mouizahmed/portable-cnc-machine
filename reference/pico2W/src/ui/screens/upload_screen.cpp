#include "ui/screens/upload_screen.h"

#include "config.h"

const UiRect UploadScreen::kAbortRect{
    static_cast<int16_t>((LCD_WIDTH - kAbortW) / 2),
    218,
    kAbortW,
    kAbortH,
};

const UiButtonStyle UploadScreen::kAbortStyle{
    COLOR_WARNING,
    COLOR_BORDER,
    COLOR_TEXT,
    2,
};

UploadScreen::UploadScreen(Ili9488& display)
    : display_(display), painter_(display) {}

void UploadScreen::render(const char* name) const {
    display_.fill_screen(COLOR_BG);

    // Title
    const char* title = "UPLOADING";
    display_.draw_text(
        static_cast<int16_t>((LCD_WIDTH - display_.text_width(title, 3)) / 2),
        52, title, COLOR_TEXT, COLOR_BG, 3);

    // Filename
    display_.draw_text(
        static_cast<int16_t>((LCD_WIDTH - display_.text_width(name, 2)) / 2),
        96, name, COLOR_MUTED, COLOR_BG, 2);

    // Abort button
    painter_.draw_button(kAbortRect, "ABORT", kAbortStyle);

    // Warning
    const char* warn = "DO NOT REMOVE STORAGE";
    display_.draw_text(
        static_cast<int16_t>((LCD_WIDTH - display_.text_width(warn, 1)) / 2),
        270, warn, COLOR_WARNING, COLOR_BG, 1);
}

bool UploadScreen::hit_test_abort(const TouchPoint& point) const {
    return ui_rect_contains(kAbortRect, point);
}
