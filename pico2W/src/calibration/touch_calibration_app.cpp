#include "calibration/touch_calibration_app.h"

#include <cstdio>

#include "config.h"
#include "pico/stdlib.h"

namespace {
constexpr uint8_t kStableSamplesNeeded = 6;
constexpr int16_t kCrosshairArm = 14;
constexpr int16_t kCrosshairBox = 10;
}  // namespace

const std::array<TouchCalibrationApp::CalibrationTarget, 4> TouchCalibrationApp::kTargets{{
    {"TOP LEFT", CALIBRATION_MARGIN_X, CALIBRATION_MARGIN_Y},
    {"TOP RIGHT", static_cast<uint16_t>(LCD_WIDTH - 1 - CALIBRATION_MARGIN_X), CALIBRATION_MARGIN_Y},
    {"BOTTOM LEFT", CALIBRATION_MARGIN_X, static_cast<uint16_t>(LCD_HEIGHT - 1 - CALIBRATION_MARGIN_Y)},
    {"BOTTOM RIGHT", static_cast<uint16_t>(LCD_WIDTH - 1 - CALIBRATION_MARGIN_X), static_cast<uint16_t>(LCD_HEIGHT - 1 - CALIBRATION_MARGIN_Y)},
}};

const TouchCalibrationApp::Button TouchCalibrationApp::kSaveButton{36, 270, 180, 38, "SAVE", COLOR_SUCCESS};
const TouchCalibrationApp::Button TouchCalibrationApp::kRetryButton{264, 270, 180, 38, "RETRY", COLOR_WARNING};

TouchCalibrationApp::TouchCalibrationApp(Ili9488& display, Xpt2046& touch, CalibrationStorage& storage)
    : display_(display), touch_(touch), storage_(storage) {}

int32_t TouchCalibrationApp::abs32(int32_t value) {
    return value < 0 ? -value : value;
}

uint16_t TouchCalibrationApp::clamp_u12(int32_t value) {
    if (value < 0) {
        return 0;
    }
    if (value > 4095) {
        return 4095;
    }
    return static_cast<uint16_t>(value);
}

int32_t TouchCalibrationApp::average_pair(uint16_t a, uint16_t b) {
    return static_cast<int32_t>(a + b) / 2;
}

void TouchCalibrationApp::draw_centered_text(int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) const {
    const int16_t width = display_.text_width(text, scale);
    const int16_t x = static_cast<int16_t>((LCD_WIDTH - width) / 2);
    display_.draw_text(x, y, text, fg, bg, scale);
}

void TouchCalibrationApp::draw_crosshair(int16_t x, int16_t y, uint16_t color) const {
    display_.fill_rect(x - kCrosshairArm, y, (kCrosshairArm * 2) + 1, 1, color);
    display_.fill_rect(x, y - kCrosshairArm, 1, (kCrosshairArm * 2) + 1, color);
    display_.draw_rect(x - (kCrosshairBox / 2), y - (kCrosshairBox / 2), kCrosshairBox, kCrosshairBox, color);
}

void TouchCalibrationApp::draw_button(const Button& button) const {
    display_.fill_rect(button.x, button.y, button.w, button.h, button.fill);
    display_.draw_rect(button.x, button.y, button.w, button.h, COLOR_BORDER);
    const int16_t text_x = static_cast<int16_t>(button.x + (button.w - display_.text_width(button.label, 2)) / 2);
    const int16_t text_y = static_cast<int16_t>(button.y + (button.h - display_.text_height(2)) / 2);
    display_.draw_text(text_x, text_y, button.label, COLOR_TEXT, button.fill, 2);
}

bool TouchCalibrationApp::button_contains(const Button& button, const TouchPoint& point) const {
    return point.x >= button.x &&
           point.x <= (button.x + button.w) &&
           point.y >= button.y &&
           point.y <= (button.y + button.h);
}

void TouchCalibrationApp::wait_for_release(uint32_t timeout_ms) const {
    const uint32_t start = to_ms_since_boot(get_absolute_time());
    while (touch_.is_touched() && (to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        sleep_ms(UI_POLL_MS);
    }
}

void TouchCalibrationApp::render_status_screen(const char* title, const char* line1, const char* line2) const {
    display_.fill_screen(COLOR_BG);
    draw_centered_text(20, title, COLOR_TEXT, COLOR_BG, 3);
    draw_centered_text(70, line1, COLOR_MUTED, COLOR_BG, 1);
    draw_centered_text(88, line2, COLOR_MUTED, COLOR_BG, 1);
}

void TouchCalibrationApp::render_target_screen(const CalibrationTarget& target, std::size_t index, const char* footer) const {
    char step[24];
    std::snprintf(step, sizeof(step), "POINT %u OF %u", static_cast<unsigned int>(index + 1), static_cast<unsigned int>(kTargets.size()));

    render_status_screen("TOUCH CALIBRATION", "PRESS AND HOLD THE TARGET", step);
    display_.draw_text(18, 126, target.label, COLOR_TEXT, COLOR_BG, 2);
    display_.draw_text(18, 152, footer, COLOR_MUTED, COLOR_BG, 1);
    draw_crosshair(static_cast<int16_t>(target.x), static_cast<int16_t>(target.y), COLOR_TARGET);
}

bool TouchCalibrationApp::collect_target_sample(const CalibrationTarget& target, std::size_t index, RawTouchPoint& sample) const {
    std::array<RawTouchPoint, kStableSamplesNeeded> captures{};

    while (true) {
        wait_for_release();
        render_target_screen(target, index, "WAITING FOR TOUCH");

        while (!touch_.is_touched()) {
            sleep_ms(UI_POLL_MS);
        }

        render_target_screen(target, index, "HOLD STEADY");
        sleep_ms(TOUCH_SETTLE_MS);

        std::size_t count = 0;
        while (touch_.is_touched() && count < captures.size()) {
            RawTouchPoint raw{};
            if (touch_.read_raw_touch_relaxed(raw, 0)) {
                captures[count++] = raw;
            }
            sleep_ms(25);
        }

        wait_for_release();

        if (count == captures.size()) {
            uint32_t total_x = 0;
            uint32_t total_y = 0;
            for (const RawTouchPoint& raw : captures) {
                total_x += raw.x;
                total_y += raw.y;
            }

            sample.x = static_cast<uint16_t>(total_x / captures.size());
            sample.y = static_cast<uint16_t>(total_y / captures.size());
            sample.z = 0;

            render_target_screen(target, index, "CAPTURED");
            draw_crosshair(static_cast<int16_t>(target.x), static_cast<int16_t>(target.y), COLOR_SUCCESS);
            sleep_ms(250);
            return true;
        }

        render_target_screen(target, index, "TOUCH TOO SHORT - TRY AGAIN");
        sleep_ms(450);
    }
}

void TouchCalibrationApp::compute_axis_range(int32_t raw_low,
                                             int32_t raw_high,
                                             int32_t target_low,
                                             int32_t target_high,
                                             int32_t screen_max,
                                             uint16_t& out_min,
                                             uint16_t& out_max) {
    const int32_t raw_span = (raw_high > raw_low) ? (raw_high - raw_low) : 1;
    const int32_t target_span = (target_high > target_low) ? (target_high - target_low) : 1;

    const int32_t projected_min = raw_low - ((target_low * raw_span) + (target_span / 2)) / target_span;
    const int32_t projected_max = projected_min + ((screen_max * raw_span) + (target_span / 2)) / target_span;

    out_min = clamp_u12(projected_min);
    out_max = clamp_u12(projected_max);

    if (out_max <= out_min) {
        out_max = static_cast<uint16_t>(out_min + 1);
    }
}

TouchCalibration TouchCalibrationApp::calculate_calibration(const std::array<RawTouchPoint, 4>& raw_points) const {
    const int32_t left_x = average_pair(raw_points[0].x, raw_points[2].x);
    const int32_t right_x = average_pair(raw_points[1].x, raw_points[3].x);
    const int32_t top_x = average_pair(raw_points[0].x, raw_points[1].x);
    const int32_t bottom_x = average_pair(raw_points[2].x, raw_points[3].x);

    const int32_t left_y = average_pair(raw_points[0].y, raw_points[2].y);
    const int32_t right_y = average_pair(raw_points[1].y, raw_points[3].y);
    const int32_t top_y = average_pair(raw_points[0].y, raw_points[1].y);
    const int32_t bottom_y = average_pair(raw_points[2].y, raw_points[3].y);

    const int32_t no_swap_score = abs32(right_x - left_x) + abs32(bottom_y - top_y);
    const int32_t swap_score = abs32(right_y - left_y) + abs32(bottom_x - top_x);
    const bool swap_xy = swap_score > no_swap_score;

    const int32_t axis_x_left = swap_xy ? left_y : left_x;
    const int32_t axis_x_right = swap_xy ? right_y : right_x;
    const int32_t axis_y_top = swap_xy ? top_x : top_y;
    const int32_t axis_y_bottom = swap_xy ? bottom_x : bottom_y;

    TouchCalibration calibration{};
    calibration.swap_xy = swap_xy;
    calibration.invert_x = axis_x_right < axis_x_left;
    calibration.invert_y = axis_y_bottom < axis_y_top;

    const int32_t x_low = (axis_x_left < axis_x_right) ? axis_x_left : axis_x_right;
    const int32_t x_high = (axis_x_left < axis_x_right) ? axis_x_right : axis_x_left;
    const int32_t y_low = (axis_y_top < axis_y_bottom) ? axis_y_top : axis_y_bottom;
    const int32_t y_high = (axis_y_top < axis_y_bottom) ? axis_y_bottom : axis_y_top;

    compute_axis_range(x_low, x_high, CALIBRATION_MARGIN_X, LCD_WIDTH - 1 - CALIBRATION_MARGIN_X, LCD_WIDTH - 1, calibration.x_min, calibration.x_max);
    compute_axis_range(y_low, y_high, CALIBRATION_MARGIN_Y, LCD_HEIGHT - 1 - CALIBRATION_MARGIN_Y, LCD_HEIGHT - 1, calibration.y_min, calibration.y_max);
    return calibration;
}

void TouchCalibrationApp::render_live_values(const TouchPoint& mapped, const RawTouchPoint& raw) const {
    char line[40];

    display_.fill_rect(36, 108, 408, 56, COLOR_BG);

    std::snprintf(line, sizeof(line), "X:%3u Y:%3u", mapped.x, mapped.y);
    draw_centered_text(112, line, COLOR_TRACE, COLOR_BG, 2);

    std::snprintf(line, sizeof(line), "RAW X:%4u Y:%4u Z:%4u", raw.x, raw.y, raw.z);
    draw_centered_text(140, line, COLOR_TEXT, COLOR_BG, 1);
}

void TouchCalibrationApp::render_touch_test_screen(const TouchCalibration& calibration, bool loaded_from_flash, bool show_buttons) const {
    char line[48];

    display_.fill_screen(COLOR_BG);
    draw_centered_text(20, loaded_from_flash ? "TOUCH TEST" : "CALIBRATION REVIEW", COLOR_TEXT, COLOR_BG, 3);
    draw_centered_text(64, loaded_from_flash ? "HOLD TOUCH 2S TO RECALIBRATE" : "TEST TOUCH THEN SAVE OR RETRY", COLOR_MUTED, COLOR_BG, 1);
    draw_centered_text(82, "DRAW AROUND THE SCREEN", COLOR_MUTED, COLOR_BG, 1);
    display_.draw_rect(8, 172, LCD_WIDTH - 16, 88, COLOR_BORDER);

    std::snprintf(line, sizeof(line), "XMIN:%u XMAX:%u", calibration.x_min, calibration.x_max);
    display_.draw_text(24, 272, line, COLOR_TEXT, COLOR_BG, 1);
    std::snprintf(line, sizeof(line), "YMIN:%u YMAX:%u", calibration.y_min, calibration.y_max);
    display_.draw_text(24, 288, line, COLOR_TEXT, COLOR_BG, 1);
    std::snprintf(line, sizeof(line), "SWAP:%u INVX:%u INVY:%u",
                  calibration.swap_xy ? 1u : 0u,
                  calibration.invert_x ? 1u : 0u,
                  calibration.invert_y ? 1u : 0u);
    display_.draw_text(240, 288, line, COLOR_MUTED, COLOR_BG, 1);

    render_live_values(TouchPoint{0, 0}, RawTouchPoint{0, 0, 0});

    if (show_buttons) {
        draw_button(kSaveButton);
        draw_button(kRetryButton);
    }
}

TouchCalibrationApp::ReviewAction TouchCalibrationApp::review_calibration(const TouchCalibration& calibration) const {
    render_touch_test_screen(calibration, false, true);

    uint32_t last_touch_ms = 0;

    while (true) {
        TouchPoint mapped{};
        RawTouchPoint raw{};
        const bool have_mapped = touch_.read_touch(mapped);
        const bool have_raw = touch_.read_raw_touch_relaxed(raw, 0);

        if (have_mapped && have_raw) {
            render_live_values(mapped, raw);
            display_.fill_rect(static_cast<int16_t>(mapped.x - 2), static_cast<int16_t>(mapped.y - 2), 5, 5, COLOR_TRACE);

            const uint32_t now = to_ms_since_boot(get_absolute_time());
            if ((now - last_touch_ms) >= 250) {
                if (button_contains(kSaveButton, mapped)) {
                    wait_for_release();
                    return ReviewAction::Save;
                }
                if (button_contains(kRetryButton, mapped)) {
                    wait_for_release();
                    return ReviewAction::Retry;
                }
                last_touch_ms = now;
            }
        }

        sleep_ms(UI_POLL_MS);
    }
}

void TouchCalibrationApp::touch_test_loop(const TouchCalibration& calibration) const {
    render_touch_test_screen(calibration, true, false);

    bool touching = false;
    uint32_t touch_started_ms = 0;

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const bool pressed = touch_.is_touched();

        if (pressed) {
            if (!touching) {
                touching = true;
                touch_started_ms = now;
            } else if ((now - touch_started_ms) >= RECALIBRATE_HOLD_MS) {
                wait_for_release();
                return;
            }
        } else {
            touching = false;
        }

        TouchPoint mapped{};
        RawTouchPoint raw{};
        const bool have_mapped = touch_.read_touch(mapped);
        const bool have_raw = touch_.read_raw_touch_relaxed(raw, 0);

        if (have_mapped && have_raw) {
            render_live_values(mapped, raw);
            display_.fill_rect(static_cast<int16_t>(mapped.x - 2), static_cast<int16_t>(mapped.y - 2), 5, 5, COLOR_TRACE);
        }

        sleep_ms(UI_POLL_MS);
    }
}

void TouchCalibrationApp::run_touch_test(const TouchCalibration& calibration) const {
    touch_test_loop(calibration);
}

TouchCalibration TouchCalibrationApp::calibrate_touch() const {
    render_status_screen("TOUCH CALIBRATION", "NO SAVED CALIBRATION OR REENTRY", "FOLLOW THE FOUR TARGETS");
    sleep_ms(900);

    std::array<RawTouchPoint, 4> raw_points{};
    for (std::size_t i = 0; i < kTargets.size(); ++i) {
        collect_target_sample(kTargets[i], i, raw_points[i]);
    }

    TouchCalibration calibration = calculate_calibration(raw_points);
    std::printf("CAL XMIN=%u XMAX=%u YMIN=%u YMAX=%u SWAP=%u INVX=%u INVY=%u\n",
                calibration.x_min,
                calibration.x_max,
                calibration.y_min,
                calibration.y_max,
                calibration.swap_xy ? 1u : 0u,
                calibration.invert_x ? 1u : 0u,
                calibration.invert_y ? 1u : 0u);
    return calibration;
}

TouchCalibrationApp::CalibrationResult TouchCalibrationApp::ensure_calibration(TouchCalibration& calibration) const {
    if (storage_.load(calibration)) {
        touch_.set_calibration(calibration);
        return CalibrationResult::LoadedFromStorage;
    }

    while (true) {
        calibration = calibrate_touch();
        if (!CalibrationStorage::has_reasonable_ranges(calibration)) {
            render_status_screen("CALIBRATION FAILED", "CAPTURED RANGE LOOKED INVALID", "RETRYING");
            sleep_ms(1000);
            continue;
        }

        touch_.set_calibration(calibration);
        const ReviewAction action = review_calibration(calibration);
        if (action == ReviewAction::Save) {
            if (storage_.save(calibration)) {
                return CalibrationResult::CalibratedAndSaved;
            }
            render_status_screen("FLASH SAVE FAILED", "CALIBRATION WAS NOT STORED", "TRY SAVE AGAIN OR RETRY");
            sleep_ms(1200);
        }
    }
}
