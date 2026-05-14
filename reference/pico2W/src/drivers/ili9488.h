#pragma once

#include <cstddef>
#include <cstdint>

#include "hardware/spi.h"

class Ili9488 {
public:
    explicit Ili9488(spi_inst_t* spi);

    void init();
    void set_rotation(uint8_t rotation);

    void fill_screen(uint16_t color);
    void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void draw_rgb565_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* pixels);
    void draw_text(int16_t x, int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale = 1);

    int16_t text_width(const char* text, uint8_t scale = 1) const;
    int16_t text_height(uint8_t scale = 1) const;

private:
    spi_inst_t* spi_;

    void reset();
    void write_command(uint8_t command);
    void write_data(const uint8_t* data, std::size_t size);
    void write_data8(uint8_t value);
    void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void push_color(uint16_t color, std::size_t count);
    void draw_char(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t scale);
};
