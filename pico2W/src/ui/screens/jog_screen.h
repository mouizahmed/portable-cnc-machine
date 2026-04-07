#pragma once

#include <array>

#include "app/jog/jog_state_machine.h"
#include "ui/components/app_frame.h"
#include "ui/helpers/ui_helpers.h"
#include "ui/screens/screen.h"

class JogScreen : public Screen {
public:
    struct ButtonSpec {
        UiRect rect;
        const char* label;
        JogAction action;
    };

    JogScreen(Ili9488& display, AppFrame& frame, JogStateMachine& model);

    NavTab tab() const override;
    void render(const StatusSnapshot& status) override;
    void render_content() override;
    UiEventResult handle_event(const UiEvent& event) override;

private:
    enum class ButtonGroup : uint8_t {
        None,
        Jog,
        Step,
        Feed,
        Action,
    };

    struct ButtonHit {
        ButtonGroup group = ButtonGroup::None;
        uint8_t index = 0;
        JogAction action = JogAction::MoveXPositive;
    };

    static const UiRect kPositionPanelRect;
    static const UiRect kJogPanelRect;
    static const UiRect kStepPanelRect;
    static const UiRect kFeedPanelRect;
    static const std::array<ButtonSpec, 6> kJogButtons;
    static const std::array<ButtonSpec, 3> kStepButtons;
    static const std::array<ButtonSpec, 3> kFeedButtons;
    static const std::array<ButtonSpec, 2> kActionButtons;
    static const UiPanelStyle kPanelStyle;

    Ili9488& display_;
    UiPainter painter_;
    AppFrame& frame_;
    JogStateMachine& model_;
    ButtonGroup pressed_group_ = ButtonGroup::None;
    uint8_t pressed_index_ = 0;

    void draw_static_layout() const;
    void draw_dynamic_content() const;
    void draw_position_panel() const;
    void draw_position_axes() const;
    void draw_position_settings() const;
    void draw_jog_buttons() const;
    void draw_step_buttons() const;
    void draw_feed_buttons() const;
    void draw_action_buttons() const;
    void draw_jog_button(uint8_t index) const;
    void draw_step_button(uint8_t index) const;
    void draw_feed_button(uint8_t index) const;
    void draw_action_button(uint8_t index) const;
    UiRect position_info_rect() const;
    UiRect position_axes_rect() const;
    UiRect position_settings_rect() const;
    void draw_button_scaled(const UiRect& rect, const char* label, uint16_t fill, uint8_t scale, uint16_t text_color = COLOR_TEXT) const;
    void draw_button(const UiRect& rect, const char* label, uint16_t fill, uint16_t text_color = COLOR_TEXT) const;
    void render_region(const UiRect& region) const;
    void redraw_dirty(const DirtyRectList& dirty_regions) const;
    bool hit_test_button(const TouchPoint& point, ButtonHit& hit) const;
    UiRect button_rect(ButtonGroup group, uint8_t index) const;
    bool is_pressed(ButtonGroup group, uint8_t index) const;
};
