#include "ui/helpers/ui_helpers.h"

#include "ui/layout/ui_layout.h"

namespace {
bool ui_rect_is_empty(const UiRect& rect) {
    return rect.w <= 0 || rect.h <= 0;
}
}  // namespace

bool ui_rect_contains(const UiRect& rect, const TouchPoint& point) {
    return !ui_rect_is_empty(rect) &&
           point.x >= static_cast<uint16_t>(rect.x) &&
           point.x <= static_cast<uint16_t>(rect.x + rect.w) &&
           point.y >= static_cast<uint16_t>(rect.y) &&
           point.y <= static_cast<uint16_t>(rect.y + rect.h);
}

bool ui_rect_intersects(const UiRect& a, const UiRect& b) {
    if (ui_rect_is_empty(a) || ui_rect_is_empty(b)) {
        return false;
    }

    return a.x < (b.x + b.w) &&
           (a.x + a.w) > b.x &&
           a.y < (b.y + b.h) &&
           (a.y + a.h) > b.y;
}

UiPainter::UiPainter(Ili9488& display) : display_(display) {}

void UiPainter::fill_rect(const UiRect& rect, uint16_t color) const {
    if (ui_rect_is_empty(rect)) {
        return;
    }
    display_.fill_rect(rect.x, rect.y, rect.w, rect.h, color);
}

void UiPainter::draw_rect(const UiRect& rect, uint16_t color) const {
    if (ui_rect_is_empty(rect)) {
        return;
    }
    display_.draw_rect(rect.x, rect.y, rect.w, rect.h, color);
}

void UiPainter::draw_panel(const UiRect& rect, const char* header, const UiPanelStyle& style) const {
    fill_rect(rect, style.fill);
    draw_rect(rect, style.border);

    if (header != nullptr && style.header_height > 0) {
        const UiRect header_rect{
            static_cast<int16_t>(rect.x + 1),
            static_cast<int16_t>(rect.y + 1),
            static_cast<int16_t>(rect.w - 2),
            style.header_height,
        };
        fill_rect(header_rect, style.header_fill);
        const uint8_t header_scale = 2;
        const int16_t text_x = static_cast<int16_t>(rect.x + (rect.w - display_.text_width(header, header_scale)) / 2);
        const int16_t text_y = static_cast<int16_t>(rect.y + 1 + (style.header_height - display_.text_height(header_scale)) / 2);
        display_.draw_text(text_x, text_y, header, style.header_text, style.header_fill, header_scale);
    }
}

void UiPainter::draw_button(const UiRect& rect, const char* label, const UiButtonStyle& style) const {
    if (label == nullptr || ui_rect_is_empty(rect)) {
        return;
    }

    fill_rect(rect, style.fill);
    draw_rect(rect, style.border);

    const int16_t text_x = static_cast<int16_t>(rect.x + (rect.w - display_.text_width(label, style.text_scale)) / 2);
    const int16_t text_y = static_cast<int16_t>(rect.y + (rect.h - display_.text_height(style.text_scale)) / 2);
    display_.draw_text(text_x, text_y, label, style.text, style.fill, style.text_scale);
}

void UiPainter::draw_text_centered(int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) const {
    if (text == nullptr) {
        return;
    }

    const int16_t width = display_.text_width(text, scale);
    const int16_t x = static_cast<int16_t>((LCD_WIDTH - width) / 2);
    display_.draw_text(x, y, text, fg, bg, scale);
}

void DirtyRectList::clear() {
    count_ = 0;
}

void DirtyRectList::add(const UiRect& rect) {
    if (ui_rect_is_empty(rect) || count_ >= rects_.size()) {
        return;
    }
    rects_[count_++] = rect;
}

std::size_t DirtyRectList::size() const {
    return count_;
}

const UiRect& DirtyRectList::operator[](std::size_t index) const {
    return rects_[index];
}
