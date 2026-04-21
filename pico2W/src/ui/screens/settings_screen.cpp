#include "ui/screens/settings_screen.h"

#include <cstdio>
#include <cstring>

#include "config.h"
#include "ui/layout/ui_layout.h"

namespace {
constexpr uint16_t kColorPanel = rgb565(30, 40, 50);
constexpr uint16_t kColorPanelAccent = rgb565(38, 52, 66);
constexpr uint16_t kColorPanelBody = rgb565(26, 35, 44);
constexpr uint16_t kColorSelection = rgb565(42, 132, 108);
constexpr uint16_t kColorButton = rgb565(54, 90, 150);
constexpr uint16_t kColorButtonAlt = rgb565(72, 84, 102);
constexpr uint16_t kColorButtonWarn = rgb565(156, 96, 34);
constexpr uint16_t kColorButtonDanger = rgb565(156, 62, 50);
constexpr uint16_t kColorToggleOn = rgb565(48, 176, 96);
constexpr uint16_t kColorToggleOff = rgb565(156, 62, 50);

constexpr int16_t kPageHeaderHeight = 22;
constexpr int16_t kPanelInset = 12;
constexpr int16_t kTopBarStackWidth = 108;
constexpr int16_t kTopBarStackInsetRight = 12;
constexpr int16_t kTopBarStackLineHeight = 8;
constexpr int16_t kTopBarStackGap = 3;
constexpr int16_t kFieldRowHeight = 24;
constexpr int16_t kFieldRowGap = 4;
constexpr int16_t kFooterButtonHeight = 24;
constexpr int16_t kFooterButtonGap = 12;
constexpr int16_t kEditorButtonWidth = 64;
constexpr int16_t kEditorButtonHeight = 28;
constexpr int16_t kEditorTitleY = 12;
constexpr int16_t kEditorValueY = 32;
constexpr int16_t kEditorButtonsY = 56;
constexpr std::size_t kMaxStatusChars = 22;

constexpr float kMinStepsPerMm = 1.0f;
constexpr float kMaxStepsPerMm = 20000.0f;
constexpr float kMinFeedRate = 1.0f;
constexpr float kMaxFeedRate = 50000.0f;
constexpr float kMinAcceleration = 1.0f;
constexpr float kMaxAcceleration = 10000.0f;
constexpr float kMinTravel = 1.0f;
constexpr float kMaxTravel = 5000.0f;
constexpr float kMinSpindleRpm = 0.0f;
constexpr float kMaxSpindleRpm = 50000.0f;
constexpr float kMinTemperature = 0.0f;
constexpr float kMaxTemperatureLimit = 150.0f;
} // namespace

const UiPanelStyle SettingsScreen::kPanelStyle{
    kColorPanel,
    COLOR_BORDER,
    kColorPanelAccent,
    COLOR_TEXT,
    UiLayout::kPanelHeaderHeight,
};

const UiButtonStyle SettingsScreen::kActionButtonStyle{
    kColorButton,
    COLOR_BORDER,
    COLOR_TEXT,
    1,
};

const UiButtonStyle SettingsScreen::kSelectedButtonStyle{
    kColorSelection,
    COLOR_BORDER,
    COLOR_TEXT,
    1,
};

const UiButtonStyle SettingsScreen::kValueButtonStyle{
    kColorButtonAlt,
    COLOR_BORDER,
    COLOR_TEXT,
    1,
};

const UiRect SettingsScreen::kListPanelRect{0, UiLayout::kContentTopY, 282, UiLayout::kContentHeight};
const UiRect SettingsScreen::kEditorPanelRect{
    static_cast<int16_t>(kListPanelRect.x + kListPanelRect.w),
    UiLayout::kContentTopY,
    static_cast<int16_t>(LCD_WIDTH - kListPanelRect.w),
    UiLayout::kContentHeight,
};

const std::array<SettingsScreen::PageSpec, 4> SettingsScreen::kPages{{
    {"PROFILE", {FieldId::StepsPerMmX, FieldId::StepsPerMmY, FieldId::StepsPerMmZ, FieldId::MaxTravelX, FieldId::MaxTravelY, FieldId::MaxTravelZ}, 6},
    {"MOTION", {FieldId::MaxFeedRateX, FieldId::MaxFeedRateY, FieldId::MaxFeedRateZ, FieldId::AccelerationX, FieldId::AccelerationY, FieldId::AccelerationZ}, 6},
    {"SAFETY", {FieldId::SoftLimitsEnabled, FieldId::HardLimitsEnabled, FieldId::WarningTemperature, FieldId::MaxTemperature, FieldId::WarningTemperature, FieldId::MaxTemperature}, 4},
    {"SPINDLE", {FieldId::SpindleMinRpm, FieldId::SpindleMaxRpm, FieldId::SpindleMinRpm, FieldId::SpindleMaxRpm, FieldId::SpindleMinRpm, FieldId::SpindleMaxRpm}, 2},
}};

SettingsScreen::SettingsScreen(Ili9488& display, AppFrame& frame, PortableCncController& controller)
    : display_(display),
      painter_(display),
      frame_(frame),
      controller_(controller) {
    set_status("Loaded", false);
}

NavTab SettingsScreen::tab() const {
    return NavTab::Settings;
}

void SettingsScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Settings);
    render_content();
}

void SettingsScreen::render_content() {
    ensure_initialized();
    if (!has_pending_changes() &&
        !settings_equal(clean_settings_, controller_.machine_settings())) {
        reset_from_controller();
    }
    draw_list_panel();
    draw_editor_panel();
    draw_footer_actions();
    draw_top_bar_status();
}

UiEventResult SettingsScreen::handle_event(const UiEvent& event) {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    ensure_initialized();

    HitResult hit{};
    if (!hit_test(event.touch, hit)) {
        return UiEventResult{};
    }

    const uint8_t previous_page = current_page_;
    const uint8_t previous_index = selected_index_;
    bool redraw_list_rows = false;
    bool redraw_selected_row = false;
    bool redraw_editor_preview = false;
    bool redraw_footer = false;
    bool redraw_top_bar = false;

    switch (hit.target) {
        case HitTarget::FieldRow:
            if (hit.index < current_page().field_count) {
                selected_index_ = hit.index;
                if (selected_index_ != previous_index) {
                    draw_field_row(previous_index);
                    draw_field_row(selected_index_);
                    redraw_editor_preview = true;
                }
            }
            break;
        case HitTarget::PrevPage:
            set_selected_page(current_page_ == 0 ? static_cast<uint8_t>(kPages.size() - 1) : static_cast<uint8_t>(current_page_ - 1));
            redraw_list_rows = true;
            redraw_editor_preview = true;
            redraw_footer = true;
            break;
        case HitTarget::NextPage:
            set_selected_page(static_cast<uint8_t>((current_page_ + 1) % kPages.size()));
            redraw_list_rows = true;
            redraw_editor_preview = true;
            redraw_footer = true;
            break;
        case HitTarget::Save: {
            const char* error_reason = nullptr;
            if (controller_.save_machine_settings(editable_settings_, &error_reason)) {
                clean_settings_ = editable_settings_;
                set_status("Saved", false);
            } else {
                set_status(error_reason != nullptr ? error_reason : "Save failed", true);
            }
            redraw_top_bar = true;
            break;
        }
        case HitTarget::Revert:
            editable_settings_ = clean_settings_;
            set_status("Reverted", false);
            redraw_list_rows = true;
            redraw_editor_preview = true;
            redraw_top_bar = true;
            break;
        case HitTarget::Defaults:
            editable_settings_ = MachineSettingsStore::defaults();
            set_status("Defaults loaded", false);
            redraw_list_rows = true;
            redraw_editor_preview = true;
            redraw_top_bar = true;
            break;
        case HitTarget::Decrease:
            change_selected_value(-1);
            redraw_selected_row = true;
            redraw_editor_preview = true;
            redraw_top_bar = true;
            break;
        case HitTarget::Increase:
            change_selected_value(1);
            redraw_selected_row = true;
            redraw_editor_preview = true;
            redraw_top_bar = true;
            break;
        case HitTarget::Toggle:
            toggle_selected_value();
            redraw_selected_row = true;
            redraw_editor_preview = true;
            redraw_top_bar = true;
            break;
        case HitTarget::None:
            break;
    }

    if (current_page_ != previous_page) {
        draw_page_header();
        redraw_list_rows = true;
    }

    if (redraw_list_rows) {
        draw_list_rows();
    } else if (redraw_selected_row) {
        draw_field_row(selected_index_);
    }
    if (redraw_editor_preview) {
        draw_editor_preview();
    }
    if (redraw_footer) {
        draw_footer_actions();
    }
    if (redraw_top_bar) {
        draw_top_bar_status();
    }

    return UiEventResult{true, false, tab()};
}

void SettingsScreen::ensure_initialized() {
    if (initialized_) {
        return;
    }
    reset_from_controller();
    initialized_ = true;
}

void SettingsScreen::reset_from_controller() {
    clean_settings_ = controller_.machine_settings();
    editable_settings_ = clean_settings_;
    current_page_ = 0;
    selected_index_ = 0;
    set_status("Loaded", false);
}

void SettingsScreen::draw_top_bar_status() const {
    const int16_t x = static_cast<int16_t>(LCD_WIDTH - kTopBarStackWidth - kTopBarStackInsetRight);
    const UiRect rect{x, 1, kTopBarStackWidth, static_cast<int16_t>(UiLayout::kTopBarHeight - 2)};
    painter_.fill_rect(rect, COLOR_HEADER);

    const char* pending_text = has_pending_changes() ? "PENDING" : "UNCHANGED";
    const uint16_t pending_color = has_pending_changes() ? COLOR_TRACE : COLOR_SUCCESS;
    const int16_t block_height = static_cast<int16_t>((kTopBarStackLineHeight * 2) + kTopBarStackGap);
    const int16_t top_y = static_cast<int16_t>(rect.y + (rect.h - block_height) / 2);
    const int16_t pending_x = static_cast<int16_t>(x + rect.w - display_.text_width(pending_text, 1));
    const int16_t status_x = static_cast<int16_t>(x + rect.w - display_.text_width(status_line_, 1));

    display_.draw_text(pending_x, top_y, pending_text, pending_color, COLOR_HEADER, 1);
    display_.draw_text(status_x,
                       static_cast<int16_t>(top_y + kTopBarStackLineHeight + kTopBarStackGap),
                       status_line_,
                       status_is_error_ ? COLOR_WARNING : COLOR_MUTED,
                       COLOR_HEADER,
                       1);
}

void SettingsScreen::draw_page_header() const {
    const UiRect body = UiLayout::panel_body_rect(kListPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(
        UiRect{
            static_cast<int16_t>(body.x + kPanelInset),
            static_cast<int16_t>(body.y + kEditorTitleY),
            static_cast<int16_t>(body.w - (kPanelInset * 2)),
            10,
        },
        kColorPanelBody);
    char line[40];
    std::snprintf(line, sizeof(line), "PAGE %u/4  %s", static_cast<unsigned>(current_page_ + 1), current_page().title);
    display_.draw_text(static_cast<int16_t>(body.x + kPanelInset),
                       static_cast<int16_t>(body.y + kEditorTitleY),
                       line,
                       COLOR_TEXT,
                       kColorPanelBody,
                       1);
}

void SettingsScreen::draw_field_row(uint8_t index) const {
    if (index >= current_page().field_count) {
        return;
    }

    const UiRect row = field_row_rect(index);
    const bool selected = index == selected_index_;
    painter_.fill_rect(row, selected ? kColorSelection : kColorPanel);
    painter_.draw_rect(row, COLOR_BORDER);

    char value[32];
    format_field_value(current_page().fields[index], value, sizeof(value));
    display_.draw_text(static_cast<int16_t>(row.x + 8), static_cast<int16_t>(row.y + 7),
                       field_label(current_page().fields[index]), COLOR_TEXT,
                       selected ? kColorSelection : kColorPanel, 1);
    const int16_t value_x = static_cast<int16_t>(row.x + row.w - display_.text_width(value, 1) - 8);
    display_.draw_text(value_x, static_cast<int16_t>(row.y + 7),
                       value, COLOR_TEXT,
                       selected ? kColorSelection : kColorPanel, 1);
}

void SettingsScreen::clear_unused_field_rows() const {
    for (uint8_t i = current_page().field_count; i < 6; ++i) {
        painter_.fill_rect(field_row_rect(i), kColorPanelBody);
    }
}

void SettingsScreen::draw_list_rows() const {
    for (uint8_t i = 0; i < current_page().field_count; ++i) {
        draw_field_row(i);
    }
    clear_unused_field_rows();
}

void SettingsScreen::draw_list_panel() const {
    painter_.draw_panel(kListPanelRect, "SETTINGS", kPanelStyle);
    painter_.fill_rect(UiLayout::panel_body_rect(kListPanelRect, kPanelStyle.header_height), kColorPanelBody);
    draw_page_header();
    draw_list_rows();
}

UiRect SettingsScreen::editor_preview_rect() const {
    const UiRect body = UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height);
    const int16_t bottom = static_cast<int16_t>(footer_button_rect(0).y - 8);
    return UiRect{body.x, body.y, body.w, static_cast<int16_t>(bottom - body.y)};
}

void SettingsScreen::draw_editor_preview() const {
    const UiRect body = UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(editor_preview_rect(), kColorPanelBody);

    const FieldId field = selected_field();
    const char* label = field_label(field);
    char value[32];
    format_field_value(field, value, sizeof(value));

    const auto draw_centered_in_body = [&](int16_t y, const char* text, uint16_t fg, uint8_t scale) {
        const int16_t x = static_cast<int16_t>(body.x + (body.w - display_.text_width(text, scale)) / 2);
        display_.draw_text(x, y, text, fg, kColorPanelBody, scale);
    };

    draw_centered_in_body(static_cast<int16_t>(body.y + kEditorTitleY), label, COLOR_TEXT, 1);
    draw_centered_in_body(static_cast<int16_t>(body.y + kEditorValueY), value, COLOR_TARGET, 2);

    if (field_is_toggle(field)) {
        UiButtonStyle style{
            std::strcmp(value, "ON") == 0 ? kColorToggleOn : kColorToggleOff,
            COLOR_BORDER,
            COLOR_TEXT,
            1,
        };
        painter_.draw_button(toggle_button_rect(), std::strcmp(value, "ON") == 0 ? "ON" : "OFF", style);
    } else {
        painter_.draw_button(decrease_button_rect(), "-", kValueButtonStyle);
        painter_.draw_button(increase_button_rect(), "+", kValueButtonStyle);
    }
}

void SettingsScreen::draw_editor_panel() const {
    painter_.draw_panel(kEditorPanelRect, "EDITOR", kPanelStyle);
    painter_.fill_rect(UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height), kColorPanelBody);
    draw_editor_preview();
}

void SettingsScreen::draw_footer_actions() const {
    static constexpr const char* kLabels[5] = {"PREV", "NEXT", "SAVE", "REVERT", "DEFAULTS"};
    for (uint8_t i = 0; i < 5; ++i) {
        UiButtonStyle style = kActionButtonStyle;
        if (i == 2) {
            style.fill = COLOR_BUTTON;
        } else if (i >= 3) {
            style.fill = i == 3 ? kColorButtonDanger : kColorButtonWarn;
        }
        painter_.draw_button(footer_button_rect(i), kLabels[i], style);
    }
}

UiRect SettingsScreen::field_row_rect(uint8_t index) const {
    const UiRect body = UiLayout::panel_body_rect(kListPanelRect, kPanelStyle.header_height);
    return UiRect{
        static_cast<int16_t>(body.x + kPanelInset),
        static_cast<int16_t>(body.y + 28 + index * (kFieldRowHeight + kFieldRowGap)),
        static_cast<int16_t>(body.w - (kPanelInset * 2)),
        kFieldRowHeight,
    };
}

UiRect SettingsScreen::footer_button_rect(uint8_t index) const {
    const UiRect body = UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height);
    const int16_t left_x = static_cast<int16_t>(body.x + kPanelInset);
    const int16_t button_width =
        static_cast<int16_t>((body.w - (kPanelInset * 2) - kFooterButtonGap) / 2);
    const int16_t right_x = static_cast<int16_t>(left_x + button_width + kFooterButtonGap);
    const int16_t full_width = static_cast<int16_t>(body.w - (kPanelInset * 2));
    const int16_t row3_y = static_cast<int16_t>(body.y + body.h - kPanelInset - kFooterButtonHeight);
    const int16_t row2_y = static_cast<int16_t>(row3_y - kFooterButtonHeight - kFooterButtonGap);
    const int16_t row1_y = static_cast<int16_t>(row2_y - kFooterButtonHeight - kFooterButtonGap);

    switch (index) {
        case 0:
            return UiRect{left_x, row1_y, button_width, kFooterButtonHeight};
        case 1:
            return UiRect{right_x, row1_y, button_width, kFooterButtonHeight};
        case 2:
            return UiRect{left_x, row2_y, button_width, kFooterButtonHeight};
        case 3:
            return UiRect{right_x, row2_y, button_width, kFooterButtonHeight};
        case 4:
        default:
            return UiRect{left_x, row3_y, full_width, kFooterButtonHeight};
    }
}

UiRect SettingsScreen::decrease_button_rect() const {
    const UiRect body = UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height);
    const int16_t left_x = static_cast<int16_t>(body.x + kPanelInset);
    return UiRect{
        left_x,
        static_cast<int16_t>(body.y + kEditorButtonsY),
        kEditorButtonWidth,
        kEditorButtonHeight,
    };
}

UiRect SettingsScreen::increase_button_rect() const {
    const UiRect body = UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height);
    return UiRect{
        static_cast<int16_t>(body.x + body.w - kPanelInset - kEditorButtonWidth),
        static_cast<int16_t>(body.y + kEditorButtonsY),
        kEditorButtonWidth,
        kEditorButtonHeight,
    };
}

UiRect SettingsScreen::toggle_button_rect() const {
    const UiRect body = UiLayout::panel_body_rect(kEditorPanelRect, kPanelStyle.header_height);
    const int16_t width = static_cast<int16_t>(body.w - (kPanelInset * 2));
    return UiRect{
        static_cast<int16_t>(body.x + kPanelInset),
        static_cast<int16_t>(body.y + kEditorButtonsY),
        width,
        kEditorButtonHeight,
    };
}

bool SettingsScreen::hit_test(const TouchPoint& point, HitResult& hit) const {
    for (uint8_t i = 0; i < current_page().field_count; ++i) {
        if (ui_rect_contains(field_row_rect(i), point)) {
            hit = HitResult{HitTarget::FieldRow, i};
            return true;
        }
    }

    for (uint8_t i = 0; i < 5; ++i) {
        if (ui_rect_contains(footer_button_rect(i), point)) {
            hit.target = static_cast<HitTarget>(static_cast<uint8_t>(HitTarget::PrevPage) + i);
            return true;
        }
    }

    if (field_is_toggle(selected_field()) && ui_rect_contains(toggle_button_rect(), point)) {
        hit.target = HitTarget::Toggle;
        return true;
    }

    if (!field_is_toggle(selected_field())) {
        if (ui_rect_contains(decrease_button_rect(), point)) {
            hit.target = HitTarget::Decrease;
            return true;
        }
        if (ui_rect_contains(increase_button_rect(), point)) {
            hit.target = HitTarget::Increase;
            return true;
        }
    }

    return false;
}

SettingsScreen::FieldId SettingsScreen::selected_field() const {
    return current_page().fields[selected_index_];
}

const SettingsScreen::PageSpec& SettingsScreen::current_page() const {
    return kPages[current_page_];
}

const char* SettingsScreen::field_label(FieldId field) const {
    switch (field) {
        case FieldId::StepsPerMmX: return "Steps/mm X";
        case FieldId::StepsPerMmY: return "Steps/mm Y";
        case FieldId::StepsPerMmZ: return "Steps/mm Z";
        case FieldId::MaxTravelX: return "Travel X";
        case FieldId::MaxTravelY: return "Travel Y";
        case FieldId::MaxTravelZ: return "Travel Z";
        case FieldId::MaxFeedRateX: return "Feed X";
        case FieldId::MaxFeedRateY: return "Feed Y";
        case FieldId::MaxFeedRateZ: return "Feed Z";
        case FieldId::AccelerationX: return "Accel X";
        case FieldId::AccelerationY: return "Accel Y";
        case FieldId::AccelerationZ: return "Accel Z";
        case FieldId::SoftLimitsEnabled: return "Soft limits";
        case FieldId::HardLimitsEnabled: return "Hard limits";
        case FieldId::WarningTemperature: return "Warn temp";
        case FieldId::MaxTemperature: return "Max temp";
        case FieldId::SpindleMinRpm: return "Spindle min";
        case FieldId::SpindleMaxRpm: return "Spindle max";
    }
    return "";
}

void SettingsScreen::format_field_value(FieldId field, char* buffer, std::size_t size) const {
    switch (field) {
        case FieldId::StepsPerMmX: std::snprintf(buffer, size, "%.1f", static_cast<double>(editable_settings_.steps_per_mm_x)); break;
        case FieldId::StepsPerMmY: std::snprintf(buffer, size, "%.1f", static_cast<double>(editable_settings_.steps_per_mm_y)); break;
        case FieldId::StepsPerMmZ: std::snprintf(buffer, size, "%.1f", static_cast<double>(editable_settings_.steps_per_mm_z)); break;
        case FieldId::MaxTravelX: std::snprintf(buffer, size, "%.1f", static_cast<double>(editable_settings_.max_travel_x)); break;
        case FieldId::MaxTravelY: std::snprintf(buffer, size, "%.1f", static_cast<double>(editable_settings_.max_travel_y)); break;
        case FieldId::MaxTravelZ: std::snprintf(buffer, size, "%.1f", static_cast<double>(editable_settings_.max_travel_z)); break;
        case FieldId::MaxFeedRateX: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.max_feed_rate_x)); break;
        case FieldId::MaxFeedRateY: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.max_feed_rate_y)); break;
        case FieldId::MaxFeedRateZ: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.max_feed_rate_z)); break;
        case FieldId::AccelerationX: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.acceleration_x)); break;
        case FieldId::AccelerationY: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.acceleration_y)); break;
        case FieldId::AccelerationZ: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.acceleration_z)); break;
        case FieldId::SoftLimitsEnabled: std::snprintf(buffer, size, "%s", editable_settings_.soft_limits_enabled ? "ON" : "OFF"); break;
        case FieldId::HardLimitsEnabled: std::snprintf(buffer, size, "%s", editable_settings_.hard_limits_enabled ? "ON" : "OFF"); break;
        case FieldId::WarningTemperature: std::snprintf(buffer, size, "%.0f C", static_cast<double>(editable_settings_.warning_temperature)); break;
        case FieldId::MaxTemperature: std::snprintf(buffer, size, "%.0f C", static_cast<double>(editable_settings_.max_temperature)); break;
        case FieldId::SpindleMinRpm: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.spindle_min_rpm)); break;
        case FieldId::SpindleMaxRpm: std::snprintf(buffer, size, "%.0f", static_cast<double>(editable_settings_.spindle_max_rpm)); break;
    }
}

bool SettingsScreen::field_is_toggle(FieldId field) const {
    return field == FieldId::SoftLimitsEnabled || field == FieldId::HardLimitsEnabled;
}

void SettingsScreen::change_selected_value(int direction) {
    const MachineSettings before = editable_settings_;
    auto adjust = [direction](float& value, float step, float min, float max) {
        value = clamp_value(value + (step * static_cast<float>(direction)), min, max);
    };

    switch (selected_field()) {
        case FieldId::StepsPerMmX: adjust(editable_settings_.steps_per_mm_x, 10.0f, kMinStepsPerMm, kMaxStepsPerMm); break;
        case FieldId::StepsPerMmY: adjust(editable_settings_.steps_per_mm_y, 10.0f, kMinStepsPerMm, kMaxStepsPerMm); break;
        case FieldId::StepsPerMmZ: adjust(editable_settings_.steps_per_mm_z, 10.0f, kMinStepsPerMm, kMaxStepsPerMm); break;
        case FieldId::MaxTravelX: adjust(editable_settings_.max_travel_x, 5.0f, kMinTravel, kMaxTravel); break;
        case FieldId::MaxTravelY: adjust(editable_settings_.max_travel_y, 5.0f, kMinTravel, kMaxTravel); break;
        case FieldId::MaxTravelZ: adjust(editable_settings_.max_travel_z, 5.0f, kMinTravel, kMaxTravel); break;
        case FieldId::MaxFeedRateX: adjust(editable_settings_.max_feed_rate_x, 100.0f, kMinFeedRate, kMaxFeedRate); break;
        case FieldId::MaxFeedRateY: adjust(editable_settings_.max_feed_rate_y, 100.0f, kMinFeedRate, kMaxFeedRate); break;
        case FieldId::MaxFeedRateZ: adjust(editable_settings_.max_feed_rate_z, 50.0f, kMinFeedRate, kMaxFeedRate); break;
        case FieldId::AccelerationX: adjust(editable_settings_.acceleration_x, 10.0f, kMinAcceleration, kMaxAcceleration); break;
        case FieldId::AccelerationY: adjust(editable_settings_.acceleration_y, 10.0f, kMinAcceleration, kMaxAcceleration); break;
        case FieldId::AccelerationZ: adjust(editable_settings_.acceleration_z, 10.0f, kMinAcceleration, kMaxAcceleration); break;
        case FieldId::WarningTemperature:
            adjust(editable_settings_.warning_temperature, 1.0f, kMinTemperature, editable_settings_.max_temperature);
            break;
        case FieldId::MaxTemperature:
            adjust(editable_settings_.max_temperature, 1.0f, editable_settings_.warning_temperature, kMaxTemperatureLimit);
            break;
        case FieldId::SpindleMinRpm:
            adjust(editable_settings_.spindle_min_rpm, 100.0f, kMinSpindleRpm, editable_settings_.spindle_max_rpm);
            break;
        case FieldId::SpindleMaxRpm:
            adjust(editable_settings_.spindle_max_rpm, 100.0f, editable_settings_.spindle_min_rpm, kMaxSpindleRpm);
            break;
        case FieldId::SoftLimitsEnabled:
        case FieldId::HardLimitsEnabled:
            break;
    }

    if (!settings_equal(before, editable_settings_)) {
        set_status("Edited", false);
    }
}

void SettingsScreen::toggle_selected_value() {
    const MachineSettings before = editable_settings_;
    switch (selected_field()) {
        case FieldId::SoftLimitsEnabled:
            editable_settings_.soft_limits_enabled = !editable_settings_.soft_limits_enabled;
            break;
        case FieldId::HardLimitsEnabled:
            editable_settings_.hard_limits_enabled = !editable_settings_.hard_limits_enabled;
            break;
        default:
            break;
    }

    if (!settings_equal(before, editable_settings_)) {
        set_status("Edited", false);
    }
}

void SettingsScreen::set_selected_page(uint8_t page) {
    current_page_ = page;
    selected_index_ = 0;
}

void SettingsScreen::set_status(const char* text, bool is_error) {
    const char* source = text != nullptr ? text : "";
    if (std::strlen(source) <= kMaxStatusChars) {
        std::snprintf(status_line_, sizeof(status_line_), "%s", source);
    } else {
        std::snprintf(status_line_, sizeof(status_line_), "%.*s...", static_cast<int>(kMaxStatusChars - 3), source);
    }
    status_is_error_ = is_error;
}

bool SettingsScreen::has_pending_changes() const {
    return !settings_equal(editable_settings_, clean_settings_);
}

bool SettingsScreen::settings_equal(const MachineSettings& left, const MachineSettings& right) {
    return left.steps_per_mm_x == right.steps_per_mm_x &&
           left.steps_per_mm_y == right.steps_per_mm_y &&
           left.steps_per_mm_z == right.steps_per_mm_z &&
           left.max_feed_rate_x == right.max_feed_rate_x &&
           left.max_feed_rate_y == right.max_feed_rate_y &&
           left.max_feed_rate_z == right.max_feed_rate_z &&
           left.acceleration_x == right.acceleration_x &&
           left.acceleration_y == right.acceleration_y &&
           left.acceleration_z == right.acceleration_z &&
           left.max_travel_x == right.max_travel_x &&
           left.max_travel_y == right.max_travel_y &&
           left.max_travel_z == right.max_travel_z &&
           left.soft_limits_enabled == right.soft_limits_enabled &&
           left.hard_limits_enabled == right.hard_limits_enabled &&
           left.spindle_min_rpm == right.spindle_min_rpm &&
           left.spindle_max_rpm == right.spindle_max_rpm &&
           left.warning_temperature == right.warning_temperature &&
           left.max_temperature == right.max_temperature;
}

float SettingsScreen::clamp_value(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}
