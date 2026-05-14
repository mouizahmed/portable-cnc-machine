#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "services/portable_cnc_controller.h"
#include "ui/components/app_frame.h"
#include "ui/helpers/ui_helpers.h"
#include "ui/screens/screen.h"

class SettingsScreen : public Screen {
public:
    SettingsScreen(Ili9488& display, AppFrame& frame, PortableCncController& controller);

    NavTab tab() const override;
    void render(const StatusSnapshot& status) override;
    void render_content() override;
    UiEventResult handle_event(const UiEvent& event) override;

private:
    enum class FieldId : uint8_t {
        StepsPerMmX,
        StepsPerMmY,
        StepsPerMmZ,
        MaxTravelX,
        MaxTravelY,
        MaxTravelZ,
        MaxFeedRateX,
        MaxFeedRateY,
        MaxFeedRateZ,
        AccelerationX,
        AccelerationY,
        AccelerationZ,
        SoftLimitsEnabled,
        HardLimitsEnabled,
        WarningTemperature,
        MaxTemperature,
        SpindleMinRpm,
        SpindleMaxRpm,
    };

    struct PageSpec {
        const char* title;
        std::array<FieldId, 6> fields;
        uint8_t field_count;
    };

    enum class HitTarget : uint8_t {
        None,
        FieldRow,
        PrevPage,
        NextPage,
        Save,
        Revert,
        Defaults,
        Decrease,
        Increase,
        Toggle,
    };

    struct HitResult {
        HitTarget target = HitTarget::None;
        uint8_t index = 0;
    };

    static const std::array<PageSpec, 4> kPages;
    static const UiPanelStyle kPanelStyle;
    static const UiButtonStyle kActionButtonStyle;
    static const UiButtonStyle kSelectedButtonStyle;
    static const UiButtonStyle kValueButtonStyle;
    static const UiRect kListPanelRect;
    static const UiRect kEditorPanelRect;

    Ili9488& display_;
    UiPainter painter_;
    AppFrame& frame_;
    PortableCncController& controller_;
    MachineSettings editable_settings_{};
    MachineSettings clean_settings_{};
    bool initialized_ = false;
    uint8_t current_page_ = 0;
    uint8_t selected_index_ = 0;
    char status_line_[64]{};
    bool status_is_error_ = false;

    void ensure_initialized();
    void reset_from_controller();
    void draw_top_bar_status() const;
    void draw_page_header() const;
    void draw_field_row(uint8_t index) const;
    void clear_unused_field_rows() const;
    void draw_list_rows() const;
    void draw_list_panel() const;
    void draw_editor_preview() const;
    void draw_editor_panel() const;
    void draw_footer_actions() const;
    UiRect field_row_rect(uint8_t index) const;
    UiRect footer_button_rect(uint8_t index) const;
    UiRect decrease_button_rect() const;
    UiRect increase_button_rect() const;
    UiRect toggle_button_rect() const;
    UiRect editor_preview_rect() const;
    bool hit_test(const TouchPoint& point, HitResult& hit) const;
    FieldId selected_field() const;
    const PageSpec& current_page() const;
    const char* field_label(FieldId field) const;
    void format_field_value(FieldId field, char* buffer, std::size_t size) const;
    bool field_is_toggle(FieldId field) const;
    void change_selected_value(int direction);
    void toggle_selected_value();
    void set_selected_page(uint8_t page);
    void set_status(const char* text, bool is_error);
    bool has_pending_changes() const;
    static bool settings_equal(const MachineSettings& left, const MachineSettings& right);
    static float clamp_value(float value, float min, float max);
};
