#include "ui/screens/upload_screen.h"

#include <cstdio>

#include "config.h"

namespace {
constexpr int16_t kBarX = 40;
constexpr int16_t kBarY = 134;
constexpr int16_t kBarW = 400;
constexpr int16_t kBarH = 22;
constexpr int16_t kPctY = 170;
constexpr int16_t kAbortW = 140;
constexpr int16_t kAbortH = 32;
}  // namespace

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

void UploadScreen::render(uint32_t bytes, uint32_t total, const char* name) const {
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

    // Progress bar border
    display_.draw_rect(kBarX, kBarY, kBarW, kBarH, COLOR_BORDER);

    // Abort button
    painter_.draw_button(kAbortRect, "ABORT", kAbortStyle);

    // Warning
    const char* warn = "DO NOT REMOVE STORAGE";
    display_.draw_text(
        static_cast<int16_t>((LCD_WIDTH - display_.text_width(warn, 1)) / 2),
        270, warn, COLOR_WARNING, COLOR_BG, 1);

    render_progress(bytes, total);
}

void UploadScreen::render_progress(uint32_t bytes, uint32_t total) const {
    // Redraw bar fill
    display_.fill_rect(kBarX + 1, kBarY + 1, kBarW - 2, kBarH - 2, COLOR_BG);
    if (total > 0 && bytes > 0) {
        int16_t fill_w = static_cast<int16_t>(
            static_cast<uint64_t>(bytes) * (kBarW - 2) / total);
        if (fill_w > kBarW - 2) fill_w = kBarW - 2;
        if (fill_w > 0)
            display_.fill_rect(kBarX + 1, kBarY + 1, fill_w, kBarH - 2, COLOR_SUCCESS);
    }

    // Percentage
    char pct[8];
    if (total > 0) {
        std::snprintf(pct, sizeof(pct), "%lu%%",
                      static_cast<unsigned long>(
                          static_cast<uint64_t>(bytes) * 100 / total));
    } else {
        std::snprintf(pct, sizeof(pct), "---");
    }
    display_.fill_rect(0, kPctY, static_cast<int16_t>(LCD_WIDTH), 20, COLOR_BG);
    display_.draw_text(
        static_cast<int16_t>((LCD_WIDTH - display_.text_width(pct, 2)) / 2),
        kPctY, pct, COLOR_TEXT, COLOR_BG, 2);
}

bool UploadScreen::hit_test_abort(const TouchPoint& point) const {
    return ui_rect_contains(kAbortRect, point);
}
