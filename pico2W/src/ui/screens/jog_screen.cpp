#include "ui/screens/jog_screen.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "ui/layout/ui_layout.h"

namespace {
constexpr uint16_t kColorPanel = rgb565(30, 40, 50);
constexpr uint16_t kColorPanelAccent = rgb565(38, 52, 66);
constexpr uint16_t kColorPanelBody = rgb565(26, 35, 44);
constexpr uint16_t kColorJogButtonAlt = rgb565(74, 86, 102);
constexpr uint16_t kColorJogButtonZ = rgb565(62, 112, 176);
constexpr uint16_t kColorSelection = rgb565(42, 132, 108);
constexpr uint16_t kColorAction = rgb565(176, 116, 38);
constexpr uint16_t kColorHome = rgb565(48, 176, 96);
constexpr uint16_t kColorPadTile = COLOR_BUTTON;
constexpr uint16_t kColorPadCenter = kColorJogButtonAlt;
constexpr uint16_t kColorPadPressed = rgb565(30, 72, 130);
constexpr uint16_t kColorButtonAltPressed = rgb565(58, 70, 88);
constexpr uint16_t kColorSelectionPressed = rgb565(34, 108, 88);
constexpr uint16_t kColorActionPressed = rgb565(142, 92, 28);
constexpr uint16_t kColorHomePressed = rgb565(38, 142, 76);

template <std::size_t N>
bool hit_test_buttons(const std::array<JogScreen::ButtonSpec, N>& buttons, const TouchPoint& point, JogAction& action) {
    for (const JogScreen::ButtonSpec& button : buttons) {
        if (ui_rect_contains(button.rect, point)) {
            action = button.action;
            return true;
        }
    }

    return false;
}
}  // namespace

const UiRect JogScreen::kPositionPanelRect{0, UiLayout::kContentTopY, 160, UiLayout::kContentHeight};
const UiRect JogScreen::kJogPanelRect{
    static_cast<int16_t>(kPositionPanelRect.x + kPositionPanelRect.w),
    UiLayout::kContentTopY,
    160,
    UiLayout::kContentHeight,
};
const UiRect JogScreen::kStepPanelRect{
    static_cast<int16_t>(kJogPanelRect.x + kJogPanelRect.w),
    UiLayout::kContentTopY,
    160,
    static_cast<int16_t>(UiLayout::kContentHeight / 2),
};
const UiRect JogScreen::kFeedPanelRect{
    kStepPanelRect.x,
    static_cast<int16_t>(kStepPanelRect.y + kStepPanelRect.h),
    kStepPanelRect.w,
    static_cast<int16_t>(UiLayout::kContentHeight - kStepPanelRect.h),
};
const UiPanelStyle JogScreen::kPanelStyle{
    kColorPanel,
    COLOR_BORDER,
    kColorPanelAccent,
    COLOR_TEXT,
    UiLayout::kPanelHeaderHeight,
};

const std::array<JogScreen::ButtonSpec, 6> JogScreen::kJogButtons{{
    {{static_cast<int16_t>(kJogPanelRect.x + 62), static_cast<int16_t>(kJogPanelRect.y + 42), 36, 36}, "Y+", JogAction::MoveYPositive},
    {{static_cast<int16_t>(kJogPanelRect.x + 62), static_cast<int16_t>(kJogPanelRect.y + 124), 36, 36}, "Y-", JogAction::MoveYNegative},
    {{static_cast<int16_t>(kJogPanelRect.x + 20), static_cast<int16_t>(kJogPanelRect.y + 83), 36, 36}, "X-", JogAction::MoveXNegative},
    {{static_cast<int16_t>(kJogPanelRect.x + 104), static_cast<int16_t>(kJogPanelRect.y + 83), 36, 36}, "X+", JogAction::MoveXPositive},
    {{static_cast<int16_t>(kJogPanelRect.x + 18), static_cast<int16_t>(kJogPanelRect.y + 177), 56, 30}, "Z+", JogAction::MoveZPositive},
    {{static_cast<int16_t>(kJogPanelRect.x + 86), static_cast<int16_t>(kJogPanelRect.y + 177), 56, 30}, "Z-", JogAction::MoveZNegative},
}};

const std::array<JogScreen::ButtonSpec, 3> JogScreen::kStepButtons{{
    {{static_cast<int16_t>(kStepPanelRect.x + 15), static_cast<int16_t>(kStepPanelRect.y + 52), 40, 32}, "0.1", JogAction::SelectStepFine},
    {{static_cast<int16_t>(kStepPanelRect.x + 60), static_cast<int16_t>(kStepPanelRect.y + 52), 40, 32}, "1.0", JogAction::SelectStepMedium},
    {{static_cast<int16_t>(kStepPanelRect.x + 105), static_cast<int16_t>(kStepPanelRect.y + 52), 40, 32}, "10", JogAction::SelectStepCoarse},
}};

const std::array<JogScreen::ButtonSpec, 3> JogScreen::kFeedButtons{{
    {{static_cast<int16_t>(kFeedPanelRect.x + 15), static_cast<int16_t>(kFeedPanelRect.y + 52), 40, 32}, "S", JogAction::SelectFeedSlow},
    {{static_cast<int16_t>(kFeedPanelRect.x + 60), static_cast<int16_t>(kFeedPanelRect.y + 52), 40, 32}, "M", JogAction::SelectFeedNormal},
    {{static_cast<int16_t>(kFeedPanelRect.x + 105), static_cast<int16_t>(kFeedPanelRect.y + 52), 40, 32}, "F", JogAction::SelectFeedFast},
}};

const std::array<JogScreen::ButtonSpec, 2> JogScreen::kActionButtons{{
    {{static_cast<int16_t>(kPositionPanelRect.x + UiLayout::kPanelBodyInsetX),
      static_cast<int16_t>(kPositionPanelRect.y + kPositionPanelRect.h - 72),
      static_cast<int16_t>(kPositionPanelRect.w - (UiLayout::kPanelBodyInsetX * 2)),
      28},
     "HOME ALL",
     JogAction::HomeAll},
    {{static_cast<int16_t>(kPositionPanelRect.x + UiLayout::kPanelBodyInsetX),
      static_cast<int16_t>(kPositionPanelRect.y + kPositionPanelRect.h - 38),
      static_cast<int16_t>(kPositionPanelRect.w - (UiLayout::kPanelBodyInsetX * 2)),
      28},
     "ZERO XYZ",
     JogAction::ZeroAll},
}};

JogScreen::JogScreen(Ili9488& display, AppFrame& frame, PortableCncController& controller)
    : display_(display),
      painter_(display),
      frame_(frame),
      controller_(controller) {}

NavTab JogScreen::tab() const {
    return NavTab::Jog;
}

void JogScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Jog);
    render_content();
}

void JogScreen::render_content() {
    draw_static_layout();
    draw_dynamic_content();
}

UiEventResult JogScreen::handle_event(const UiEvent& event) {
    if (event.type == UiEventType::TouchReleased) {
        if (pressed_group_ == ButtonGroup::None) {
            return UiEventResult{};
        }

        DirtyRectList dirty_regions;
        dirty_regions.add(button_rect(pressed_group_, pressed_index_));
        pressed_group_ = ButtonGroup::None;
        redraw_dirty(dirty_regions);
        return UiEventResult{true, false, tab()};
    }

    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    ButtonHit hit{};
    if (!hit_test_button(event.touch, hit)) {
        return UiEventResult{};
    }

    const uint8_t previous_step = controller_.jog().step_index();
    const uint8_t previous_feed = controller_.jog().feed_index();
    pressed_group_ = hit.group;
    pressed_index_ = hit.index;

    if (!controller_.apply_jog_action(hit.action)) {
        return UiEventResult{true, false, tab()};
    }

    DirtyRectList dirty_regions;
    dirty_regions.add(button_rect(hit.group, hit.index));
    switch (hit.group) {
        case ButtonGroup::Jog:
        case ButtonGroup::Action:
            dirty_regions.add(position_axes_rect());
            break;
        case ButtonGroup::Step:
            dirty_regions.add(button_rect(ButtonGroup::Step, previous_step));
            dirty_regions.add(button_rect(ButtonGroup::Step, controller_.jog().step_index()));
            dirty_regions.add(position_settings_rect());
            break;
        case ButtonGroup::Feed:
            dirty_regions.add(button_rect(ButtonGroup::Feed, previous_feed));
            dirty_regions.add(button_rect(ButtonGroup::Feed, controller_.jog().feed_index()));
            dirty_regions.add(position_settings_rect());
            break;
        case ButtonGroup::None:
            break;
    }

    redraw_dirty(dirty_regions);
    return UiEventResult{true, false, tab(), false};
}

void JogScreen::draw_static_layout() const {
    painter_.draw_panel(kPositionPanelRect, "POSITION", kPanelStyle);
    painter_.draw_panel(kJogPanelRect, "JOG PAD", kPanelStyle);
    painter_.draw_panel(kStepPanelRect, "STEP", kPanelStyle);
    painter_.draw_panel(kFeedPanelRect, "FEED", kPanelStyle);
}

void JogScreen::draw_dynamic_content() const {
    draw_position_panel();
    draw_jog_buttons();
    draw_step_buttons();
    draw_feed_buttons();
    draw_action_buttons();
}

void JogScreen::draw_position_panel() const {
    draw_position_axes();
    draw_position_settings();
}

void JogScreen::draw_position_axes() const {
    const UiRect body = position_axes_rect();
    const int16_t text_top = UiLayout::panel_text_top(kPositionPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(body, kColorPanelBody);

    auto draw_centered_line = [this](const UiRect& panel, int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) {
        const int16_t x = static_cast<int16_t>(panel.x + (panel.w - display_.text_width(text, scale)) / 2);
        display_.draw_text(x, y, text, fg, bg, scale);
    };

    char line[24];
    controller_.jog().format_axis_line('X', line, sizeof(line));
    draw_centered_line(body, text_top, line, COLOR_TEXT, kColorPanelBody, 2);
    controller_.jog().format_axis_line('Y', line, sizeof(line));
    draw_centered_line(body, static_cast<int16_t>(text_top + 30), line, COLOR_TEXT, kColorPanelBody, 2);
    controller_.jog().format_axis_line('Z', line, sizeof(line));
    draw_centered_line(body, static_cast<int16_t>(text_top + 60), line, COLOR_TEXT, kColorPanelBody, 2);
}

void JogScreen::draw_position_settings() const {
    const UiRect body = position_settings_rect();
    const int16_t text_top = UiLayout::panel_text_top(kPositionPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(body, kColorPanelBody);

    auto draw_centered_line = [this](const UiRect& panel, int16_t y, const char* text, uint16_t fg, uint16_t bg, uint8_t scale) {
        const int16_t x = static_cast<int16_t>(panel.x + (panel.w - display_.text_width(text, scale)) / 2);
        display_.draw_text(x, y, text, fg, bg, scale);
    };

    char line[24];
    std::snprintf(line, sizeof(line), "STEP  %s", controller_.jog().step_label());
    draw_centered_line(body, static_cast<int16_t>(text_top + 94), line, COLOR_MUTED, kColorPanelBody, 1);
    std::snprintf(line, sizeof(line), "FEED  %d", controller_.jog().feed_rate_mm_min());
    draw_centered_line(body, static_cast<int16_t>(text_top + 114), line, COLOR_MUTED, kColorPanelBody, 1);
}

void JogScreen::draw_jog_buttons() const {
    const UiRect body = UiLayout::panel_body_rect(kJogPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(body, kColorPanelBody);

    const UiRect center_tile{
        static_cast<int16_t>(kJogPanelRect.x + 62),
        static_cast<int16_t>(kJogPanelRect.y + 83),
        36,
        36,
    };
    for (uint8_t i = 0; i < 4; ++i) {
        draw_jog_button(i);
    }

    draw_button(center_tile, "STOP", kColorPadCenter);

    for (uint8_t i = 4; i < kJogButtons.size(); ++i) {
        draw_jog_button(i);
    }
}

void JogScreen::draw_step_buttons() const {
    const UiRect body = UiLayout::panel_body_rect(kStepPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(body, kColorPanelBody);

    for (uint8_t i = 0; i < kStepButtons.size(); ++i) {
        draw_step_button(i);
    }
}

void JogScreen::draw_feed_buttons() const {
    const UiRect body = UiLayout::panel_body_rect(kFeedPanelRect, kPanelStyle.header_height);
    painter_.fill_rect(body, kColorPanelBody);

    for (uint8_t i = 0; i < kFeedButtons.size(); ++i) {
        draw_feed_button(i);
    }
}

void JogScreen::draw_action_buttons() const {
    for (uint8_t i = 0; i < kActionButtons.size(); ++i) {
        draw_action_button(i);
    }
}

void JogScreen::draw_jog_button(uint8_t index) const {
    if (index >= kJogButtons.size()) {
        return;
    }

    const ButtonSpec& button = kJogButtons[index];
    const uint16_t fill = is_pressed(ButtonGroup::Jog, index)
                              ? (index < 4 ? kColorPadPressed : kColorButtonAltPressed)
                              : (index < 4 ? kColorPadTile : kColorJogButtonZ);
    draw_button(button.rect, button.label, fill);
}

void JogScreen::draw_step_button(uint8_t index) const {
    if (index >= kStepButtons.size()) {
        return;
    }

    const bool selected = index == controller_.jog().step_index();
    uint16_t fill = selected ? kColorSelection : kColorJogButtonAlt;
    if (is_pressed(ButtonGroup::Step, index)) {
        fill = selected ? kColorSelectionPressed : kColorButtonAltPressed;
    }
    draw_button_scaled(kStepButtons[index].rect, kStepButtons[index].label, fill, 2);
}

void JogScreen::draw_feed_button(uint8_t index) const {
    if (index >= kFeedButtons.size()) {
        return;
    }

    const bool selected = index == controller_.jog().feed_index();
    uint16_t fill = selected ? kColorSelection : kColorJogButtonAlt;
    if (is_pressed(ButtonGroup::Feed, index)) {
        fill = selected ? kColorSelectionPressed : kColorButtonAltPressed;
    }
    draw_button(kFeedButtons[index].rect, kFeedButtons[index].label, fill);
}

void JogScreen::draw_action_button(uint8_t index) const {
    if (index >= kActionButtons.size()) {
        return;
    }

    const uint16_t fill = index == 0 ? kColorHome : kColorAction;
    const uint16_t pressed_fill = index == 0 ? kColorHomePressed : kColorActionPressed;
    draw_button(kActionButtons[index].rect,
                kActionButtons[index].label,
                is_pressed(ButtonGroup::Action, index) ? pressed_fill : fill);
}

UiRect JogScreen::position_info_rect() const {
    const UiRect body = UiLayout::panel_body_rect(kPositionPanelRect, kPanelStyle.header_height);
    return UiRect{
        body.x,
        body.y,
        body.w,
        static_cast<int16_t>(kActionButtons[0].rect.y - body.y - 8),
    };
}

UiRect JogScreen::position_axes_rect() const {
    const UiRect body = position_info_rect();
    return UiRect{
        body.x,
        body.y,
        body.w,
        88,
    };
}

UiRect JogScreen::position_settings_rect() const {
    const UiRect body = position_info_rect();
    return UiRect{
        body.x,
        static_cast<int16_t>(body.y + 88),
        body.w,
        static_cast<int16_t>(body.h - 88),
    };
}

void JogScreen::draw_button(const UiRect& rect, const char* label, uint16_t fill, uint16_t text_color) const {
    const std::size_t label_length = std::strlen(label);
    const uint8_t scale = label_length <= 2 ? 2 : 1;
    draw_button_scaled(rect, label, fill, scale, text_color);
}

void JogScreen::draw_button_scaled(const UiRect& rect, const char* label, uint16_t fill, uint8_t scale, uint16_t text_color) const {
    painter_.draw_button(rect, label, UiButtonStyle{fill, COLOR_BORDER, text_color, scale});
}

void JogScreen::render_region(const UiRect& region) const {
    const UiRect position_axes = position_axes_rect();
    const UiRect position_settings = position_settings_rect();
    if (ui_rect_intersects(region, position_axes)) {
        draw_position_axes();
    }
    if (ui_rect_intersects(region, position_settings)) {
        draw_position_settings();
    }

    for (uint8_t i = 0; i < kJogButtons.size(); ++i) {
        if (ui_rect_intersects(region, kJogButtons[i].rect)) {
            draw_jog_button(i);
        }
    }

    const UiRect center_tile{
        static_cast<int16_t>(kJogPanelRect.x + 62),
        static_cast<int16_t>(kJogPanelRect.y + 83),
        36,
        36,
    };
    if (ui_rect_intersects(region, center_tile)) {
        draw_button(center_tile, "STOP", kColorPadCenter);
    }

    for (uint8_t i = 0; i < kStepButtons.size(); ++i) {
        if (ui_rect_intersects(region, kStepButtons[i].rect)) {
            draw_step_button(i);
        }
    }

    for (uint8_t i = 0; i < kFeedButtons.size(); ++i) {
        if (ui_rect_intersects(region, kFeedButtons[i].rect)) {
            draw_feed_button(i);
        }
    }

    for (uint8_t i = 0; i < kActionButtons.size(); ++i) {
        if (ui_rect_intersects(region, kActionButtons[i].rect)) {
            draw_action_button(i);
        }
    }
}

void JogScreen::redraw_dirty(const DirtyRectList& dirty_regions) const {
    for (std::size_t i = 0; i < dirty_regions.size(); ++i) {
        render_region(dirty_regions[i]);
    }
}

bool JogScreen::hit_test_button(const TouchPoint& point, ButtonHit& hit) const {
    for (uint8_t i = 0; i < kJogButtons.size(); ++i) {
        if (ui_rect_contains(kJogButtons[i].rect, point)) {
            hit = ButtonHit{ButtonGroup::Jog, i, kJogButtons[i].action};
            return true;
        }
    }

    for (uint8_t i = 0; i < kStepButtons.size(); ++i) {
        if (ui_rect_contains(kStepButtons[i].rect, point)) {
            hit = ButtonHit{ButtonGroup::Step, i, kStepButtons[i].action};
            return true;
        }
    }

    for (uint8_t i = 0; i < kFeedButtons.size(); ++i) {
        if (ui_rect_contains(kFeedButtons[i].rect, point)) {
            hit = ButtonHit{ButtonGroup::Feed, i, kFeedButtons[i].action};
            return true;
        }
    }

    for (uint8_t i = 0; i < kActionButtons.size(); ++i) {
        if (ui_rect_contains(kActionButtons[i].rect, point)) {
            hit = ButtonHit{ButtonGroup::Action, i, kActionButtons[i].action};
            return true;
        }
    }

    return false;
}

UiRect JogScreen::button_rect(ButtonGroup group, uint8_t index) const {
    switch (group) {
        case ButtonGroup::Jog:
            return index < kJogButtons.size() ? kJogButtons[index].rect : UiRect{0, 0, 0, 0};
        case ButtonGroup::Step:
            return index < kStepButtons.size() ? kStepButtons[index].rect : UiRect{0, 0, 0, 0};
        case ButtonGroup::Feed:
            return index < kFeedButtons.size() ? kFeedButtons[index].rect : UiRect{0, 0, 0, 0};
        case ButtonGroup::Action:
            return index < kActionButtons.size() ? kActionButtons[index].rect : UiRect{0, 0, 0, 0};
        case ButtonGroup::None:
            return UiRect{0, 0, 0, 0};
    }

    return UiRect{0, 0, 0, 0};
}

bool JogScreen::is_pressed(ButtonGroup group, uint8_t index) const {
    return pressed_group_ == group && pressed_index_ == index;
}
