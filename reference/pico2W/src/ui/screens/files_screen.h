#pragma once

#include <array>
#include <cstdint>

#include "protocol/desktop_protocol.h"
#include "services/portable_cnc_controller.h"
#include "ui/components/app_frame.h"
#include "ui/components/selectable_list_row.h"
#include "ui/helpers/ui_helpers.h"
#include "ui/screens/screen.h"

class FilesScreen : public Screen {
public:
    FilesScreen(Ili9488& display, AppFrame& frame, PortableCncController& controller);
    void bind_protocol(DesktopProtocol& protocol);

    NavTab tab() const override;
    void render(const StatusSnapshot& status) override;
    void render_content() override;
    UiEventResult handle_event(const UiEvent& event) override;
    void refresh_storage_view() const;

private:
    static const UiRect kListPanelRect;
    static const UiRect kDetailsPanelRect;
    static const UiRect kActionButtonRect;
    static const UiRect kRefreshButtonRect;
    static const UiPanelStyle kPanelStyle;
    static const UiButtonStyle kLoadButtonStyle;
    static const UiButtonStyle kUnloadButtonStyle;
    static const UiButtonStyle kRefreshButtonStyle;

    Ili9488& display_;
    UiPainter painter_;
    AppFrame& frame_;
    PortableCncController& controller_;
    DesktopProtocol* protocol_ = nullptr;
    SelectableListRow list_row_;
    int16_t preview_index_ = -1;

    void draw_static_layout() const;
    void draw_file_list_header() const;
    void draw_row(int16_t index) const;
    void draw_file_details() const;
    void render_region(const UiRect& region) const;
    void redraw_dirty(const DirtyRectList& dirty_regions) const;
    bool hit_test_row(const TouchPoint& point, int16_t& index) const;
    UiRect row_rect(int16_t index) const;
    void ensure_preview_selection();
    const FileEntry* preview_entry() const;
    const char* action_button_label() const;
    const UiButtonStyle* action_button_style() const;
};
