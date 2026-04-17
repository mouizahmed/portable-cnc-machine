#include "ui/screens/files_screen.h"

#include <cstdio>
#include <cstring>

#include "config.h"
#include "ui/layout/ui_layout.h"

namespace {
constexpr uint16_t kColorPanel = rgb565(30, 40, 50);
constexpr uint16_t kColorPanelAccent = rgb565(38, 52, 66);
}

const UiRect FilesScreen::kListPanelRect = UiLayout::kFilesListPanelRect;
const UiRect FilesScreen::kDetailsPanelRect = UiLayout::kFilesDetailsPanelRect;
const UiRect FilesScreen::kActionButtonRect = UiLayout::kFilesActionButtonRect;
const UiRect FilesScreen::kRefreshButtonRect = UiLayout::kFilesRefreshButtonRect;
const UiPanelStyle FilesScreen::kPanelStyle{
    kColorPanel,
    COLOR_BORDER,
    kColorPanelAccent,
    COLOR_TEXT,
    UiLayout::kPanelHeaderHeight,
};
const UiButtonStyle FilesScreen::kLoadButtonStyle{
    COLOR_SUCCESS,
    COLOR_BORDER,
    COLOR_TEXT,
    2,
};
const UiButtonStyle FilesScreen::kUnloadButtonStyle{
    COLOR_WARNING,
    COLOR_BORDER,
    COLOR_TEXT,
    2,
};
const UiButtonStyle FilesScreen::kRefreshButtonStyle{
    COLOR_MUTED,
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

void FilesScreen::bind_protocol(DesktopProtocol& protocol) {
    protocol_ = &protocol;
}

NavTab FilesScreen::tab() const {
    return NavTab::Files;
}

void FilesScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Files);
    render_content();
}

void FilesScreen::render_content() {
    ensure_preview_selection();
    draw_static_layout();

    for (std::size_t i = 0; i < controller_.jobs().count(); ++i) {
        draw_row(static_cast<int16_t>(i));
    }

    draw_file_details();
}

void FilesScreen::refresh_storage_view() const {
    const_cast<FilesScreen*>(this)->ensure_preview_selection();
    draw_static_layout();
    draw_file_details();

    for (std::size_t i = 0; i < controller_.jobs().count(); ++i) {
        draw_row(static_cast<int16_t>(i));
    }
}

UiEventResult FilesScreen::handle_event(const UiEvent& event) {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    if (ui_rect_contains(kRefreshButtonRect, event.touch)) {
        controller_.force_storage_remount();
        render_content();
        return UiEventResult{true, false, tab()};
    }

    if (action_button_label() != nullptr &&
        ui_rect_contains(kActionButtonRect, event.touch)) {
        if (protocol_ == nullptr) {
            return UiEventResult{true, false, tab()};
        }

        const int16_t previous_loaded_index = controller_.jobs().loaded_index();
        bool changed = false;
        if (controller_.jobs().has_loaded_job() &&
            (preview_index_ < 0 || preview_index_ == previous_loaded_index)) {
            changed = protocol_->unload_job();
        } else if (preview_index_ >= 0) {
            changed = protocol_->load_job_by_index(preview_index_);
        }

        if (!changed) {
            return UiEventResult{true, false, tab()};
        }

        DirtyRectList dirty_regions;
        if (previous_loaded_index >= 0) {
            dirty_regions.add(row_rect(previous_loaded_index));
        }
        if (preview_index_ >= 0) {
            dirty_regions.add(row_rect(preview_index_));
        }
        dirty_regions.add(kDetailsPanelRect);
        redraw_dirty(dirty_regions);
        return UiEventResult{true, false, tab(), true, false};
    }

    int16_t index = -1;
    if (!hit_test_row(event.touch, index)) {
        return UiEventResult{};
    }
    const int16_t previous_index = preview_index_;
    preview_index_ = index;

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
    char list_title[40];
    const uint64_t free = controller_.storage_free_bytes();
    if (free > 0) {
        const uint32_t free_mb = static_cast<uint32_t>(free / (1024u * 1024u));
        std::snprintf(list_title, sizeof(list_title), "FILES  %lu MB FREE",
                      static_cast<unsigned long>(free_mb));
    } else {
        std::snprintf(list_title, sizeof(list_title), "AVAILABLE FILES");
    }

    painter_.draw_panel(kListPanelRect, list_title, kPanelStyle);
    painter_.draw_panel(kDetailsPanelRect, "DETAILS", kPanelStyle);
    draw_file_list_header();
    painter_.draw_button(kRefreshButtonRect, "REFRESH", kRefreshButtonStyle);
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
        preview_index_ == index,
    });
}

void FilesScreen::draw_file_details() const {
    const UiRect body = UiLayout::panel_body_rect(kDetailsPanelRect, kPanelStyle.header_height);
    const int16_t text_x = UiLayout::panel_text_x(kDetailsPanelRect);
    const int16_t text_top = UiLayout::panel_text_top(kDetailsPanelRect, kPanelStyle.header_height);

    painter_.fill_rect(body, kColorPanel);

    if (controller_.jobs().count() == 0) {
        display_.draw_text(text_x, text_top, "SOURCE: STORAGE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 26), "NO G-CODE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 42), "FILES FOUND", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 76), "CHECK STORAGE", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 92), "CARD / ROOT", COLOR_MUTED, kColorPanel, 1);
        return;
    }

    const FileEntry* preview = preview_entry();
    const FileEntry* loaded = controller_.jobs().loaded_entry();
    char line[32];

    display_.draw_text(text_x, text_top, "PREVIEW", COLOR_MUTED, kColorPanel, 1);
    if (preview != nullptr) {
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 18), preview->name, COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 36), preview->summary, COLOR_MUTED, kColorPanel, 1);

        std::snprintf(line, sizeof(line), "SIZE: %s", preview->size_text);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 58), line, COLOR_TEXT, kColorPanel, 1);
        std::snprintf(line, sizeof(line), "TOOL: %s", preview->tool_text);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 78), line, COLOR_TEXT, kColorPanel, 1);
        std::snprintf(line, sizeof(line), "ZERO: %s", preview->zero_text);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 98), line, COLOR_TEXT, kColorPanel, 1);
    } else {
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 18), "NONE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 40), "TAP A FILE", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 56), "TO PREVIEW", COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 82), "SIZE: --", COLOR_MUTED, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 102), "TOOL: --", COLOR_MUTED, kColorPanel, 1);
    }

    display_.draw_text(text_x, static_cast<int16_t>(text_top + 132), "LOADED JOB", COLOR_MUTED, kColorPanel, 1);
    if (loaded != nullptr) {
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 150), loaded->name, COLOR_TEXT, kColorPanel, 1);
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 168), loaded->summary, COLOR_MUTED, kColorPanel, 1);
    } else {
        display_.draw_text(text_x, static_cast<int16_t>(text_top + 150), "NONE", COLOR_TEXT, kColorPanel, 1);
    }

    const char* action_label = action_button_label();
    const UiButtonStyle* action_style = action_button_style();
    if (action_label != nullptr && action_style != nullptr) {
        painter_.draw_button(kActionButtonRect, action_label, *action_style);
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

void FilesScreen::ensure_preview_selection() {
    if (preview_index_ >= static_cast<int16_t>(controller_.jobs().count())) {
        preview_index_ = -1;
    }
}

const FileEntry* FilesScreen::preview_entry() const {
    if (preview_index_ < 0 ||
        preview_index_ >= static_cast<int16_t>(controller_.jobs().count())) {
        return nullptr;
    }

    return &controller_.jobs().entry(static_cast<std::size_t>(preview_index_));
}

const char* FilesScreen::action_button_label() const {
    if (!controller_.can_load_file()) {
        return nullptr;
    }

    const int16_t loaded_index = controller_.jobs().loaded_index();
    if (controller_.jobs().has_loaded_job() &&
        (preview_index_ < 0 || preview_index_ == loaded_index)) {
        return "UNLOAD";
    }

    if (preview_index_ >= 0) {
        return "LOAD FILE";
    }

    return nullptr;
}

const UiButtonStyle* FilesScreen::action_button_style() const {
    const char* label = action_button_label();
    if (label == nullptr) {
        return nullptr;
    }

    return std::strcmp(label, "UNLOAD") == 0 ? &kUnloadButtonStyle : &kLoadButtonStyle;
}
