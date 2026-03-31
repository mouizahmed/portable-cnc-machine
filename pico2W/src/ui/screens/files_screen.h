#pragma once

#include <array>
#include <cstdint>

#include "app/job/job_state_machine.h"
#include "ui/components/app_frame.h"
#include "ui/components/selectable_list_row.h"
#include "ui/helpers/ui_helpers.h"
#include "ui/screens/screen.h"

class FilesScreen : public Screen {
public:
    FilesScreen(Ili9488& display, AppFrame& frame, JobStateMachine& model);

    NavTab tab() const override;
    void render(const StatusSnapshot& status) override;
    UiEventResult handle_event(const UiEvent& event) override;
    void refresh_storage_view() const;

private:
    static const UiRect kListPanelRect;
    static const UiRect kDetailsPanelRect;
    static const UiRect kRunButtonRect;
    static const UiPanelStyle kPanelStyle;
    static const UiButtonStyle kRunButtonStyle;

    Ili9488& display_;
    UiPainter painter_;
    AppFrame& frame_;
    JobStateMachine& model_;
    SelectableListRow list_row_;

    void draw_static_layout() const;
    void draw_file_list_header() const;
    void draw_row(int16_t index) const;
    void draw_file_details() const;
    void render_region(const UiRect& region) const;
    void redraw_dirty(const DirtyRectList& dirty_regions) const;
    bool hit_test_row(const TouchPoint& point, int16_t& index) const;
    UiRect row_rect(int16_t index) const;
};
