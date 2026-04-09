#include "ui/screens/files_screen.h"

#include <cstdio>

#include "config.h"
#include "ui/layout/ui_layout.h"

namespace {
constexpr uint16_t kColorPanel = rgb565(30, 40, 50);
constexpr uint16_t kColorPanelAccent = rgb565(38, 52, 66);
}  // namespace

const UiRect FilesScreen::kListPanelRect = UiLayout::kFilesListPanelRect;
const UiRect FilesScreen::kDetailsPanelRect = UiLayout::kFilesDetailsPanelRect;
const UiRect FilesScreen::kRunButtonRect = UiLayout::kFilesRunButtonRect;
const UiPanelStyle FilesScreen::kPanelStyle{
    kColorPanel,
    COLOR_BORDER,
    kColorPanelAccent,
    COLOR_TEXT,
    UiLayout::kPanelHeaderHeight,
};
const UiButtonStyle FilesScreen::kRunButtonStyle{
    COLOR_SUCCESS,
    COLOR_BORDER,
    COLOR_TEXT,
    2,
};

FilesScreen::FilesScreen(Ili9488& display, AppFrame& frame, PortableCncController& controller)
    : display_(display),
      painter_(display),
      frame_(frame),
      controller_(controller),
      list_row_(display) {}

NavTab FilesScreen::tab() const {
    return NavTab::Files;
}

void FilesScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Files);
    render_content();
}

void FilesScreen::render_content() {
    draw_static_layout();

    for (std::size_t i = 0; i < controller_.jobs().count(); ++i) {
        draw_row(static_cast<int16_t>(i));
    }

    draw_file_details();
}

void FilesScreen::refresh_storage_view() const {
    draw_file_details();
    draw_file_list_header();

    for (std::size_t i = 0; i < controller_.jobs().count(); ++i) {
        draw_row(static_cast<int16_t>(i));
    }
}

UiEventResult FilesScreen::handle_event(const UiEvent& event) {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    int16_t index = -1;
    if (!hit_test_row(event.touch, index)) {
        return UiEventResult{};
    }
    const int16_t previous_index = controller_.jobs().selected_index();
    if (!controller_.select_file(index)) {
        return UiEventResult{true, false, tab()};
    }

    DirtyRectList dirty_regions;
    if (previous_index >= 0) {
        dirty_regions.add(row_rect(previous_index));
    }
    dirty_regions.add(row_rect(index));
    dirty_regions.add(kDetailsPanelRect);

    redraw_dirty(dirty_regions);
    return UiEventResult{true, false, tab()};
}

void FilesScreen::draw_static_layout() const {
    painter_.draw_panel(kListPanelRect, "AVAILABLE FILES", kPanelStyle);
    painter_.draw_panel(kDetailsPanelRect, "DETAILS", kPanelStyle);
    draw_file_list_header();
}

void FilesScreen::draw_file_list_header() const {
    painter_.fill_rect(UiLayout::files_list_body_rect(), kColorPanel);
}

void FilesScreen::draw_row(int16_t index) const {
    const UiRect rect = row_rect(index);
    const FileEntry& file = controller_.jobs().entry(static_cast<std::size_t>(index));
    list_row_.render(SelectableListRowSpec{
        rect,
        file.name,
        file.summary,
        controller_.jobs().selected_index() == index,
    });
}

void FilesScreen::draw_file_details() const {
    const UiRect body = UiLayout::panel_body_rect(kDetailsPanelRect, kPanelStyle.header_height);
    const int16_t text_x = UiLayout::panel_text_x(kDetailsPanelRect);
    const int16_t text_top = UiLayout::panel_text_top(kDetailsPanelRect, kPanelStyle.header_height);

    painter_.fill_rect(body, kColorPanel);

    if (controller_.jobs().count() == 0) {
        display_.draw_text(text_x, text_top, "SOURCE: SD", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 26), "NO G-CODE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 42), "FILES FOUND", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 76), "CHECK SD", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 92), "CARD / ROOT", COLOR_MUTED, kColorPanel, 1);
        return;
    }

    if (!controller_.jobs().has_selection()) {
        display_.draw_text(text_x, text_top, "SOURCE: SD", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 22), "SIZE: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 44), "TOOL: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 66), "ZERO: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 96), "TAP A FILE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 112), "TO LOAD IT", COLOR_TEXT, kColorPanel, 1);
        return;
    }

    const FileEntry& file = *controller_.jobs().selected_entry();
    char line[32];

    display_.draw_text(text_x, text_top, file.name, COLOR_TEXT, kColorPanel, 1);
    display_.draw_text(text_x, static_cast<int16_t>(text_top + 20), file.summary, COLOR_MUTED, kColorPanel, 1);

    std::snprintf(line, sizeof(line), "SIZE: %s", file.size_text);
    display_.draw_text(text_x, static_cast<int16_t>(text_top + 48), line, COLOR_TEXT, kColorPanel, 1);
    std::snprintf(line, sizeof(line), "TOOL: %s", file.tool_text);
    display_.draw_text(text_x, static_cast<int16_t>(text_top + 70), line, COLOR_TEXT, kColorPanel, 1);
    std::snprintf(line, sizeof(line), "ZERO: %s", file.zero_text);
    display_.draw_text(text_x, static_cast<int16_t>(text_top + 92), line, COLOR_TEXT, kColorPanel, 1);
    if (controller_.can_run_selected_file()) {
        painter_.draw_button(kRunButtonRect, "RUN", kRunButtonStyle);
    }
}

void FilesScreen::render_region(const UiRect& region) const {
    if (ui_rect_intersects(region, kDetailsPanelRect)) {
        draw_file_details();
    }

    for (std::size_t i = 0; i < controller_.jobs().count(); ++i) {
        const UiRect row = row_rect(static_cast<int16_t>(i));
        if (ui_rect_intersects(region, row)) {
            draw_row(static_cast<int16_t>(i));
        }
    }
}

void FilesScreen::redraw_dirty(const DirtyRectList& dirty_regions) const {
    for (std::size_t i = 0; i < dirty_regions.size(); ++i) {
        render_region(dirty_regions[i]);
    }
}

bool FilesScreen::hit_test_row(const TouchPoint& point, int16_t& index) const {
    for (std::size_t i = 0; i < controller_.jobs().count(); ++i) {
        const UiRect rect = row_rect(static_cast<int16_t>(i));
        if (ui_rect_contains(rect, point)) {
            index = static_cast<int16_t>(i);
            return true;
        }
    }

    return false;
}

UiRect FilesScreen::row_rect(int16_t index) const {
    return UiLayout::files_row_rect(index);
}
