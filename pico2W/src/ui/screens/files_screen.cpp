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
    UiLayout::kFilesListHeaderHeight,
};
const UiButtonStyle FilesScreen::kRunButtonStyle{
    COLOR_SUCCESS,
    COLOR_BORDER,
    COLOR_TEXT,
    1,
};

FilesScreen::FilesScreen(Ili9488& display, AppFrame& frame, JobStateMachine& model)
    : display_(display),
      painter_(display),
      frame_(frame),
      model_(model),
      list_row_(display) {}

NavTab FilesScreen::tab() const {
    return NavTab::Files;
}

void FilesScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Files);
    frame_.draw_screen_title("FILES", "SELECT G-CODE / STORAGE");
    draw_static_layout();

    for (std::size_t i = 0; i < model_.count(); ++i) {
        draw_row(static_cast<int16_t>(i));
    }

    draw_file_details();
}

UiEventResult FilesScreen::handle_event(const UiEvent& event) {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    int16_t index = -1;
    if (!hit_test_row(event.touch, index)) {
        return UiEventResult{};
    }
    const int16_t previous_index = model_.selected_index();
    if (!model_.handle_event(JobEvent::SelectFile, index)) {
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
    const FileEntry& file = model_.entry(static_cast<std::size_t>(index));
    list_row_.render(SelectableListRowSpec{
        rect,
        file.name,
        file.summary,
        model_.selected_index() == index,
    });
}

void FilesScreen::draw_file_details() const {
    painter_.fill_rect(UiRect{
        static_cast<int16_t>(kDetailsPanelRect.x + 1),
        static_cast<int16_t>(kDetailsPanelRect.y + kPanelStyle.header_height + 2),
        static_cast<int16_t>(kDetailsPanelRect.w - 2),
        static_cast<int16_t>(kDetailsPanelRect.h - kPanelStyle.header_height - 3),
    }, kColorPanel);

    if (!model_.has_selection()) {
        display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 24, "SOURCE: SD", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 44, "SIZE: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 64, "TOOL: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 84, "ZERO: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 112, "TAP A FILE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 128, "TO LOAD IT", COLOR_TEXT, kColorPanel, 1);
        return;
    }

    const FileEntry& file = *model_.selected_entry();
    char line[32];

    display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 24, file.name, COLOR_TEXT, kColorPanel, 1);
    display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 44, file.summary, COLOR_MUTED, kColorPanel, 1);

    std::snprintf(line, sizeof(line), "SIZE: %s", file.size_text);
    display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 68, line, COLOR_TEXT, kColorPanel, 1);
    std::snprintf(line, sizeof(line), "TOOL: %s", file.tool_text);
    display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 88, line, COLOR_TEXT, kColorPanel, 1);
    std::snprintf(line, sizeof(line), "ZERO: %s", file.zero_text);
    display_.draw_text(kDetailsPanelRect.x + 10, kDetailsPanelRect.y + 108, line, COLOR_TEXT, kColorPanel, 1);
    if (model_.can_run()) {
        painter_.draw_button(kRunButtonRect, "RUN", kRunButtonStyle);
    }
}

void FilesScreen::render_region(const UiRect& region) const {
    if (ui_rect_intersects(region, kDetailsPanelRect)) {
        draw_file_details();
    }

    for (std::size_t i = 0; i < model_.count(); ++i) {
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
    for (std::size_t i = 0; i < model_.count(); ++i) {
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
