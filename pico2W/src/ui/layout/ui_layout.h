#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "ui/helpers/ui_helpers.h"

namespace UiLayout {
inline constexpr int16_t kTopBarHeight = 22;
inline constexpr int16_t kBottomBarHeight = 30;
inline constexpr int16_t kFooterStatusX = 24;
inline constexpr int16_t kFooterStatusY = 262;

inline constexpr int16_t kNavButtonWidth = 84;
inline constexpr int16_t kNavButtonHeight = kBottomBarHeight - 8;
inline constexpr int16_t kNavButtonY = LCD_HEIGHT - kBottomBarHeight + 4;
inline constexpr int16_t kNavButtonGap = 10;
inline constexpr int16_t kNavButtonStartX = 10;

inline constexpr UiRect nav_button_rect(std::size_t index) {
    return UiRect{
        static_cast<int16_t>(kNavButtonStartX + static_cast<int16_t>(index) * (kNavButtonWidth + kNavButtonGap)),
        kNavButtonY,
        kNavButtonWidth,
        kNavButtonHeight,
    };
}

inline constexpr UiRect main_menu_card_rect(std::size_t index) {
    return UiRect{
        static_cast<int16_t>(24 + static_cast<int16_t>(index % 3) * 148),
        static_cast<int16_t>((index < 3) ? 54 : 130),
        136,
        60,
    };
}

inline constexpr UiRect kFilesListPanelRect{20, 54, 292, 150};
inline constexpr UiRect kFilesDetailsPanelRect{326, 54, 134, 150};
inline constexpr UiRect kFilesRunButtonRect{
    static_cast<int16_t>(kFilesDetailsPanelRect.x + 38),
    static_cast<int16_t>(kFilesDetailsPanelRect.y + 116),
    58,
    22,
};
inline constexpr int16_t kFilesRowHeight = 28;
inline constexpr int16_t kFilesRowInsetX = 8;
inline constexpr int16_t kFilesRowHeightInner = 22;
inline constexpr int16_t kFilesListHeaderHeight = 12;

inline constexpr UiRect files_list_body_rect() {
    return UiRect{
        static_cast<int16_t>(kFilesListPanelRect.x + 8),
        static_cast<int16_t>(kFilesListPanelRect.y + 18),
        static_cast<int16_t>(kFilesListPanelRect.w - 16),
        static_cast<int16_t>(kFilesListPanelRect.h - 26),
    };
}

inline constexpr UiRect files_row_rect(int16_t index) {
    return UiRect{
        static_cast<int16_t>(kFilesListPanelRect.x + kFilesRowInsetX),
        static_cast<int16_t>(kFilesListPanelRect.y + 18 + index * kFilesRowHeight),
        static_cast<int16_t>(kFilesListPanelRect.w - (kFilesRowInsetX * 2)),
        kFilesRowHeightInner,
    };
}
}  // namespace UiLayout
