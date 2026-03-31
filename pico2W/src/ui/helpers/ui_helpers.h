#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "drivers/ili9488.h"
#include "drivers/xpt2046.h"

struct UiRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

struct UiPanelStyle {
    uint16_t fill;
    uint16_t border;
    uint16_t header_fill;
    uint16_t header_text;
    int16_t header_height;
};

struct UiButtonStyle {
    uint16_t fill;
    uint16_t border;
    uint16_t text;
    uint8_t text_scale;
};

bool ui_rect_contains(const UiRect& rect, const TouchPoint& point);
bool ui_rect_intersects(const UiRect& a, const UiRect& b);

class UiPainter {
public:
    explicit UiPainter(Ili9488& display);

    void fill_rect(const UiRect& rect, uint16_t color) const;
    void draw_rect(const UiRect& rect, uint16_t color) const;
    void draw_panel(const UiRect& rect, const char* header, const UiPanelStyle& style) const;
    void draw_button(const UiRect& rect, const char* label, const UiButtonStyle& style) const;
    void draw_text_centered(int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) const;

private:
    Ili9488& display_;
};

class DirtyRectList {
public:
    void clear();
    void add(const UiRect& rect);
    std::size_t size() const;
    const UiRect& operator[](std::size_t index) const;

private:
    static constexpr std::size_t kMaxRects = 8;

    std::array<UiRect, kMaxRects> rects_{};
    std::size_t count_ = 0;
};
