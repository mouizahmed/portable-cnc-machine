#include "drivers/ili9488.h"

#include <array>
#include <cstring>

#include "config.h"
#include "drivers/font5x7.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

namespace {
constexpr uint8_t kCmdSoftwareReset = 0x01;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdDisplayOn = 0x29;
constexpr uint8_t kCmdColumnAddressSet = 0x2A;
constexpr uint8_t kCmdPageAddressSet = 0x2B;
constexpr uint8_t kCmdMemoryWrite = 0x2C;
constexpr uint8_t kCmdMadCtl = 0x36;
constexpr uint8_t kMadCtlMy = 0x80;
constexpr uint8_t kMadCtlMx = 0x40;
constexpr uint8_t kMadCtlMv = 0x20;
constexpr uint8_t kMadCtlBgr = 0x08;

void write_pin(unsigned int pin, bool high) {
    gpio_put(pin, high ? 1 : 0);
}

uint8_t expand5_to_8(uint8_t value) {
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t expand6_to_8(uint8_t value) {
    return static_cast<uint8_t>((value << 2) | (value >> 4));
}
}  // namespace

Ili9488::Ili9488(spi_inst_t* spi) : spi_(spi) {}

void Ili9488::init() {
    spi_init(spi_, DISPLAY_SPI_BAUD);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);

    for (unsigned int pin : {PIN_LCD_RST, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_BL}) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
    }

    write_pin(PIN_LCD_CS, true);
    write_pin(PIN_LCD_DC, true);
    write_pin(PIN_LCD_BL, true);

    reset();

    write_command(0xC0);
    const uint8_t power1[] = {0x17, 0x15};
    write_data(power1, sizeof(power1));

    write_command(0xC1);
    write_data8(0x41);

    write_command(0xC5);
    const uint8_t vcom[] = {0x00, 0x12, 0x80};
    write_data(vcom, sizeof(vcom));

    write_command(0x3A);
    write_data8(0x66);

    write_command(0xB1);
    write_data8(0xA0);

    write_command(0xB4);
    write_data8(0x02);

    write_command(0xB6);
    const uint8_t display_func[] = {0x02, 0x02};
    write_data(display_func, sizeof(display_func));

    write_command(0xF7);
    const uint8_t adjust3[] = {0xA9, 0x51, 0x2C, 0x82};
    write_data(adjust3, sizeof(adjust3));

    write_command(kCmdSleepOut);
    sleep_ms(120);
    write_command(kCmdDisplayOn);
    sleep_ms(20);

    set_rotation(1);
    fill_screen(COLOR_BG);
}

void Ili9488::set_rotation(uint8_t rotation) {
    uint8_t madctl = kMadCtlBgr;

    switch (rotation % 4) {
        case 0:
            madctl |= kMadCtlMx;
            break;
        case 1:
            madctl |= kMadCtlMv;
            break;
        case 2:
            madctl |= kMadCtlMy;
            break;
        default:
            madctl |= static_cast<uint8_t>(kMadCtlMx | kMadCtlMy | kMadCtlMv);
            break;
    }

    write_command(kCmdMadCtl);
    write_data8(madctl);
}

void Ili9488::fill_screen(uint16_t color) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void Ili9488::fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }

    const int16_t x0 = x < 0 ? 0 : x;
    const int16_t y0 = y < 0 ? 0 : y;
    const int16_t x1 = (x + w - 1) >= static_cast<int16_t>(LCD_WIDTH) ? static_cast<int16_t>(LCD_WIDTH - 1) : static_cast<int16_t>(x + w - 1);
    const int16_t y1 = (y + h - 1) >= static_cast<int16_t>(LCD_HEIGHT) ? static_cast<int16_t>(LCD_HEIGHT - 1) : static_cast<int16_t>(y + h - 1);

    if (x0 > x1 || y0 > y1) {
        return;
    }

    set_window(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), static_cast<uint16_t>(x1), static_cast<uint16_t>(y1));
    push_color(color, static_cast<std::size_t>(x1 - x0 + 1) * static_cast<std::size_t>(y1 - y0 + 1));
}

void Ili9488::draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

void Ili9488::draw_text(int16_t x, int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) {
    if (text == nullptr) {
        return;
    }

    int16_t cursor_x = x;
    while (*text != '\0') {
        draw_char(cursor_x, y, *text, fg, bg, scale);
        cursor_x += static_cast<int16_t>(6 * scale);
        ++text;
    }
}

int16_t Ili9488::text_width(const char* text, uint8_t scale) const {
    if (text == nullptr) {
        return 0;
    }
    return static_cast<int16_t>(std::strlen(text) * 6 * scale);
}

int16_t Ili9488::text_height(uint8_t scale) const {
    return static_cast<int16_t>(7 * scale);
}

void Ili9488::reset() {
    write_pin(PIN_LCD_RST, false);
    sleep_ms(120);
    write_pin(PIN_LCD_RST, true);
    sleep_ms(120);
    write_command(kCmdSoftwareReset);
    sleep_ms(120);
}

void Ili9488::write_command(uint8_t command) {
    write_pin(PIN_LCD_DC, false);
    write_pin(PIN_LCD_CS, false);
    spi_write_blocking(spi_, &command, 1);
    write_pin(PIN_LCD_CS, true);
}

void Ili9488::write_data(const uint8_t* data, std::size_t size) {
    if (size == 0) {
        return;
    }

    write_pin(PIN_LCD_DC, true);
    write_pin(PIN_LCD_CS, false);
    spi_write_blocking(spi_, data, static_cast<unsigned int>(size));
    write_pin(PIN_LCD_CS, true);
}

void Ili9488::write_data8(uint8_t value) {
    write_data(&value, 1);
}

void Ili9488::set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    std::array<uint8_t, 4> buffer{
        static_cast<uint8_t>(x0 >> 8),
        static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>(x1 >> 8),
        static_cast<uint8_t>(x1 & 0xFF),
    };
    write_command(kCmdColumnAddressSet);
    write_data(buffer.data(), buffer.size());

    buffer = {
        static_cast<uint8_t>(y0 >> 8),
        static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>(y1 >> 8),
        static_cast<uint8_t>(y1 & 0xFF),
    };
    write_command(kCmdPageAddressSet);
    write_data(buffer.data(), buffer.size());

    write_command(kCmdMemoryWrite);
}

void Ili9488::push_color(uint16_t color, std::size_t count) {
    const uint8_t red = expand5_to_8(static_cast<uint8_t>((color >> 11) & 0x1F));
    const uint8_t green = expand6_to_8(static_cast<uint8_t>((color >> 5) & 0x3F));
    const uint8_t blue = expand5_to_8(static_cast<uint8_t>(color & 0x1F));

    std::array<uint8_t, 192> buffer{};
    for (std::size_t i = 0; i < buffer.size(); i += 3) {
        buffer[i] = red;
        buffer[i + 1] = green;
        buffer[i + 2] = blue;
    }

    write_pin(PIN_LCD_DC, true);
    write_pin(PIN_LCD_CS, false);

    while (count > 0) {
        const std::size_t chunk_pixels = count > (buffer.size() / 3) ? (buffer.size() / 3) : count;
        spi_write_blocking(spi_, buffer.data(), static_cast<unsigned int>(chunk_pixels * 3));
        count -= chunk_pixels;
    }

    write_pin(PIN_LCD_CS, true);
}

void Ili9488::draw_char(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t scale) {
    const auto& glyph = glyph_for(ch);

    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            const bool on = ((glyph[col] >> row) & 0x01) != 0;
            fill_rect(
                static_cast<int16_t>(x + col * scale),
                static_cast<int16_t>(y + row * scale),
                scale,
                scale,
                on ? fg : bg
            );
        }
    }

    fill_rect(
        static_cast<int16_t>(x + 5 * scale),
        y,
        scale,
        static_cast<int16_t>(7 * scale),
        bg
    );
}
