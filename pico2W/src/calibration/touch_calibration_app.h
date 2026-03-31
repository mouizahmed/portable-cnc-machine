#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "calibration/calibration_storage.h"
#include "config.h"
#include "drivers/ili9488.h"
#include "drivers/xpt2046.h"

class TouchCalibrationApp {
public:
    enum class CalibrationResult : uint8_t {
        LoadedFromStorage,
        CalibratedAndSaved,
    };

    TouchCalibrationApp(Ili9488& display, Xpt2046& touch, CalibrationStorage& storage);

    CalibrationResult ensure_calibration(TouchCalibration& calibration) const;
    void run_touch_test(const TouchCalibration& calibration) const;

private:
    struct CalibrationTarget {
        const char* label;
        uint16_t x;
        uint16_t y;
    };

    struct Button {
        int16_t x;
        int16_t y;
        int16_t w;
        int16_t h;
        const char* label;
        uint16_t fill;
    };

    enum class ReviewAction : uint8_t {
        Save,
        Retry,
    };

    static const std::array<CalibrationTarget, 4> kTargets;
    static const Button kSaveButton;
    static const Button kRetryButton;

    Ili9488& display_;
    Xpt2046& touch_;
    CalibrationStorage& storage_;

    static int32_t abs32(int32_t value);
    static uint16_t clamp_u12(int32_t value);
    static int32_t average_pair(uint16_t a, uint16_t b);

    void draw_centered_text(int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) const;
    void draw_crosshair(int16_t x, int16_t y, uint16_t color) const;
    void draw_button(const Button& button) const;
    bool button_contains(const Button& button, const TouchPoint& point) const;
    void wait_for_release(uint32_t timeout_ms = TOUCH_RELEASE_TIMEOUT_MS) const;

    void render_status_screen(const char* title, const char* line1, const char* line2) const;
    void render_target_screen(const CalibrationTarget& target, std::size_t index, const char* footer) const;
    bool collect_target_sample(const CalibrationTarget& target, std::size_t index, RawTouchPoint& sample) const;
    static void compute_axis_range(int32_t raw_low,
                                   int32_t raw_high,
                                   int32_t target_low,
                                   int32_t target_high,
                                   int32_t screen_max,
                                   uint16_t& out_min,
                                   uint16_t& out_max);
    TouchCalibration calculate_calibration(const std::array<RawTouchPoint, 4>& raw_points) const;

    void render_live_values(const TouchPoint& mapped, const RawTouchPoint& raw) const;
    void render_touch_test_screen(const TouchCalibration& calibration, bool loaded_from_flash, bool show_buttons) const;
    ReviewAction review_calibration(const TouchCalibration& calibration) const;
    void touch_test_loop(const TouchCalibration& calibration) const;
    TouchCalibration calibrate_touch() const;
};
