#include "ui/components/selectable_list_row.h"

#include "config.h"
#include "ui/layout/ui_layout.h"

namespace {
constexpr uint16_t kRowFill = rgb565(30, 40, 50);
}  // namespace

SelectableListRow::SelectableListRow(Ili9488& display) : display_(display) {}

void SelectableListRow::render(const SelectableListRowSpec& spec) const {
    const uint16_t fill = spec.selected ? COLOR_BUTTON : kRowFill;
    const uint16_t detail_color = spec.selected ? COLOR_TEXT : COLOR_MUTED;

    display_.fill_rect(spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, fill);
    display_.draw_rect(spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, COLOR_BORDER);
    display_.draw_text(static_cast<int16_t>(spec.rect.x + UiLayout::kPanelBodyInsetX), static_cast<int16_t>(spec.rect.y + 9), spec.primary_text, COLOR_TEXT, fill, 1);

    const int16_t secondary_x = static_cast<int16_t>(spec.rect.x + spec.rect.w - display_.text_width(spec.secondary_text, 1) - UiLayout::kPanelBodyInsetX);
    display_.draw_text(secondary_x, static_cast<int16_t>(spec.rect.y + 9), spec.secondary_text, detail_color, fill, 1);
}
