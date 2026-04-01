#include "drivers/xpt2046.h"

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

namespace {
constexpr uint32_t kTouchSpiBaud = 2'000'000;
constexpr int16_t kZThreshold = 180;
constexpr uint32_t kMsThreshold = 1;
}  // namespace

Xpt2046::Xpt2046(spi_inst_t* spi) : spi_(spi) {}

void Xpt2046::init() {
    gpio_init(PIN_TOUCH_CS);
    gpio_set_dir(PIN_TOUCH_CS, GPIO_OUT);
    gpio_put(PIN_TOUCH_CS, 1);

    gpio_init(PIN_TOUCH_IRQ);
    gpio_set_dir(PIN_TOUCH_IRQ, GPIO_IN);
    gpio_pull_up(PIN_TOUCH_IRQ);

    end_touch_bus();
}

bool Xpt2046::is_touched() const {
    return gpio_get(PIN_TOUCH_IRQ) == 0;
}

bool Xpt2046::read_touch(TouchPoint& point) {
    RawTouchPoint raw{};
    if (!read_raw_touch_relaxed(raw, 0)) {
        return false;
    }

    uint16_t rx = raw.x;
    uint16_t ry = raw.y;

    if (calibration_.swap_xy) {
        const uint16_t tmp = rx;
        rx = ry;
        ry = tmp;
    }

    const int32_t x_span = (calibration_.x_max > calibration_.x_min) ? static_cast<int32_t>(calibration_.x_max - calibration_.x_min) : 1;
    const int32_t y_span = (calibration_.y_max > calibration_.y_min) ? static_cast<int32_t>(calibration_.y_max - calibration_.y_min) : 1;
    const int32_t screen_x_span = (screen_x_max_ > screen_x_min_) ? static_cast<int32_t>(screen_x_max_ - screen_x_min_) : 1;
    const int32_t screen_y_span = (screen_y_max_ > screen_y_min_) ? static_cast<int32_t>(screen_y_max_ - screen_y_min_) : 1;

    int32_t x = screen_x_min_ + ((static_cast<int32_t>(rx) - calibration_.x_min) * screen_x_span / x_span);
    int32_t y = screen_y_min_ + ((static_cast<int32_t>(ry) - calibration_.y_min) * screen_y_span / y_span);

    if (calibration_.invert_x) {
        x = static_cast<int32_t>(screen_x_min_ + screen_x_max_) - x;
    }
    if (calibration_.invert_y) {
        y = static_cast<int32_t>(screen_y_min_ + screen_y_max_) - y;
    }

    if (x < 0) {
        x = 0;
    }
    if (x >= static_cast<int32_t>(LCD_WIDTH)) {
        x = LCD_WIDTH - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= static_cast<int32_t>(LCD_HEIGHT)) {
        y = LCD_HEIGHT - 1;
    }

    point.x = static_cast<uint16_t>(x);
    point.y = static_cast<uint16_t>(y);
    return true;
}

bool Xpt2046::read_raw_touch(RawTouchPoint& point, uint8_t) {
    update();
    if (zraw_ < kZThreshold || !is_touched()) {
        return false;
    }

    point.x = static_cast<uint16_t>(xraw_);
    point.y = static_cast<uint16_t>(yraw_);
    point.z = static_cast<uint16_t>(zraw_);
    return true;
}

bool Xpt2046::read_raw_touch_relaxed(RawTouchPoint& point, uint8_t) {
    update();
    if (zraw_ < kZThreshold) {
        return false;
    }

    point.x = static_cast<uint16_t>(xraw_);
    point.y = static_cast<uint16_t>(yraw_);
    point.z = static_cast<uint16_t>(zraw_);
    return true;
}

void Xpt2046::set_calibration(const TouchCalibration& calibration) {
    calibration_ = calibration;
    calibration_.x_max = (calibration_.x_max > calibration_.x_min) ? calibration_.x_max : static_cast<uint16_t>(calibration_.x_min + 1);
    calibration_.y_max = (calibration_.y_max > calibration_.y_min) ? calibration_.y_max : static_cast<uint16_t>(calibration_.y_min + 1);
}

TouchCalibration Xpt2046::calibration() const {
    return calibration_;
}

void Xpt2046::set_screen_range(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) {
    screen_x_min_ = x_min;
    screen_x_max_ = (x_max > x_min) ? x_max : static_cast<uint16_t>(x_min + 1);
    screen_y_min_ = y_min;
    screen_y_max_ = (y_max > y_min) ? y_max : static_cast<uint16_t>(y_min + 1);
}

void Xpt2046::set_rotation(uint8_t rotation) {
    rotation_ = rotation % 4;
}

void Xpt2046::update() {
    if (!is_touched()) {
        zraw_ = 0;
        return;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - msraw_) < kMsThreshold) {
        return;
    }

    int16_t data[6]{};

    begin_touch_bus();
    transfer8(0xB1);
    const int16_t z1 = static_cast<int16_t>(transfer16(0x00C1) >> 3);
    int z = z1 + 4095;
    const int16_t z2 = static_cast<int16_t>(transfer16(0x0091) >> 3);
    z -= z2;

    if (z >= kZThreshold) {
        transfer16(0x0091);
        data[0] = static_cast<int16_t>(transfer16(0x00D1) >> 3);
        data[1] = static_cast<int16_t>(transfer16(0x0091) >> 3);
        data[2] = static_cast<int16_t>(transfer16(0x00D1) >> 3);
        data[3] = static_cast<int16_t>(transfer16(0x0091) >> 3);
    }

    data[4] = static_cast<int16_t>(transfer16(0x00D0) >> 3);
    data[5] = static_cast<int16_t>(transfer16(0x0000) >> 3);
    end_touch_bus();

    if (z < 0) {
        z = 0;
    }
    if (z < kZThreshold) {
        zraw_ = 0;
        return;
    }

    zraw_ = z;
    const int16_t x = best_two_avg(data[0], data[2], data[4]);
    const int16_t y = best_two_avg(data[1], data[3], data[5]);
    msraw_ = now;

    switch (rotation_) {
        case 0:
            xraw_ = 4095 - y;
            yraw_ = x;
            break;
        case 1:
            xraw_ = x;
            yraw_ = y;
            break;
        case 2:
            xraw_ = y;
            yraw_ = 4095 - x;
            break;
        default:
            xraw_ = 4095 - x;
            yraw_ = 4095 - y;
            break;
    }
}

int16_t Xpt2046::best_two_avg(int16_t x, int16_t y, int16_t z) {
    const int16_t da = (x > y) ? (x - y) : (y - x);
    const int16_t db = (x > z) ? (x - z) : (z - x);
    const int16_t dc = (z > y) ? (z - y) : (y - z);

    if (da <= db && da <= dc) {
        return static_cast<int16_t>((x + y) >> 1);
    }
    if (db <= da && db <= dc) {
        return static_cast<int16_t>((x + z) >> 1);
    }
    return static_cast<int16_t>((y + z) >> 1);
}

uint8_t Xpt2046::transfer8(uint8_t value) {
    uint8_t result = 0;
    spi_write_read_blocking(spi_, &value, &result, 1);
    return result;
}

uint16_t Xpt2046::transfer16(uint16_t value) {
    uint8_t tx[2]{
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    uint8_t rx[2]{};
    spi_write_read_blocking(spi_, tx, rx, 2);
    return static_cast<uint16_t>((static_cast<uint16_t>(rx[0]) << 8) | rx[1]);
}

void Xpt2046::begin_touch_bus() {
    gpio_put(PIN_LCD_CS, 1);
    spi_set_baudrate(spi_, kTouchSpiBaud);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(PIN_TOUCH_CS, 0);
}

void Xpt2046::end_touch_bus() {
    gpio_put(PIN_TOUCH_CS, 1);
    spi_set_baudrate(spi_, DISPLAY_SPI_BAUD);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}
