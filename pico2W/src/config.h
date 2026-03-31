#pragma once

#include <cstdint>

constexpr unsigned int PIN_LCD_RST = 2;
constexpr unsigned int PIN_LCD_DC = 3;
constexpr unsigned int PIN_SPI_MISO = 4;
constexpr unsigned int PIN_LCD_CS = 5;
constexpr unsigned int PIN_SPI_SCK = 6;
constexpr unsigned int PIN_SPI_MOSI = 7;
constexpr unsigned int PIN_LCD_BL = 8;
constexpr unsigned int PIN_TOUCH_CS = 9;
constexpr unsigned int PIN_TOUCH_IRQ = 10;
constexpr unsigned int PIN_ESTOP = 15;

constexpr unsigned int LCD_WIDTH = 480;
constexpr unsigned int LCD_HEIGHT = 320;

constexpr uint32_t DISPLAY_SPI_BAUD = 20'000'000;
constexpr uint32_t UI_POLL_MS = 20;
constexpr uint32_t TOUCH_SETTLE_MS = 80;
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 1500;
constexpr uint32_t RECALIBRATE_HOLD_MS = 1800;
constexpr uint16_t CALIBRATION_MARGIN_X = 28;
constexpr uint16_t CALIBRATION_MARGIN_Y = 28;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) |
                                 ((g & 0xFC) << 3) |
                                 (b >> 3));
}

constexpr uint16_t COLOR_BG = rgb565(18, 22, 28);
constexpr uint16_t COLOR_HEADER = rgb565(24, 30, 38);
constexpr uint16_t COLOR_TEXT = rgb565(240, 244, 248);
constexpr uint16_t COLOR_MUTED = rgb565(160, 170, 180);
constexpr uint16_t COLOR_ACCENT = rgb565(32, 168, 120);
constexpr uint16_t COLOR_BUTTON = rgb565(46, 92, 168);
constexpr uint16_t COLOR_BUTTON_ALT = rgb565(92, 102, 118);
constexpr uint16_t COLOR_WARNING = rgb565(192, 72, 42);
constexpr uint16_t COLOR_BORDER = rgb565(88, 100, 112);
constexpr uint16_t COLOR_SUCCESS = rgb565(48, 176, 96);
constexpr uint16_t COLOR_TARGET = rgb565(68, 136, 212);
constexpr uint16_t COLOR_TRACE = rgb565(255, 210, 0);
