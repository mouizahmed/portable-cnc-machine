#pragma once

#include <cstdint>

#include "config.h"
#include "hardware/spi.h"

struct TouchPoint {
    uint16_t x;
    uint16_t y;
};

struct RawTouchPoint {
    uint16_t x;
    uint16_t y;
    uint16_t z;
};

struct TouchCalibration {
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    bool swap_xy;
    bool invert_x;
    bool invert_y;
};

class Xpt2046 {
public:
    explicit Xpt2046(spi_inst_t* spi);

    void init();
    bool is_touched() const;
    bool read_touch(TouchPoint& point);
    bool read_raw_touch(RawTouchPoint& point, uint8_t samples = 10);
    bool read_raw_touch_relaxed(RawTouchPoint& point, uint8_t samples = 8);
    void set_calibration(const TouchCalibration& calibration);
    TouchCalibration calibration() const;
    void set_screen_range(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max);
    void set_rotation(uint8_t rotation);

private:
    spi_inst_t* spi_;
    TouchCalibration calibration_{
        200,
        3900,
        200,
        3900,
        false,
        false,
        false,
    };
    uint16_t screen_x_min_ = 0;
    uint16_t screen_x_max_ = LCD_WIDTH - 1;
    uint16_t screen_y_min_ = 0;
    uint16_t screen_y_max_ = LCD_HEIGHT - 1;
    uint8_t rotation_ = 1;
    int16_t xraw_ = 0;
    int16_t yraw_ = 0;
    int16_t zraw_ = 0;
    uint32_t msraw_ = 0x80000000u;

    void update();
    static int16_t best_two_avg(int16_t x, int16_t y, int16_t z);
    uint8_t transfer8(uint8_t value);
    uint16_t transfer16(uint16_t value);
    void begin_touch_bus();
    void end_touch_bus();
};
