#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "ui/helpers/ui_helpers.h"

namespace UiLayout {
inline constexpr int16_t kTopBarHeight = 42;
inline constexpr int16_t kBottomBarHeight = 46;
inline constexpr int16_t kScreenMarginX = 0;
inline constexpr int16_t kScreenGap = 0;
inline constexpr int16_t kContentTopInset = 0;
inline constexpr int16_t kContentTopY = static_cast<int16_t>(kTopBarHeight + kContentTopInset);
inline constexpr int16_t kContentBottomY = LCD_HEIGHT - kBottomBarHeight - 1;
inline constexpr int16_t kContentHeight = static_cast<int16_t>(kContentBottomY - kContentTopY + 1);
inline constexpr int16_t kFooterStatusX = 24;
inline constexpr int16_t kFooterStatusY = 248;

inline constexpr int16_t kPanelHeaderHeight = 18;
inline constexpr int16_t kPanelHeaderTextInsetX = 10;
inline constexpr int16_t kPanelBodyInsetX = 10;
inline constexpr int16_t kPanelBodyInsetY = 10;

inline constexpr int16_t kMainMenuColumns = 2;
inline constexpr int16_t kMainMenuRows = 2;
inline constexpr int16_t kMainMenuCardWidth = LCD_WIDTH / kMainMenuColumns;
inline constexpr int16_t kMainMenuCardHeight = kContentHeight / kMainMenuRows;

inline constexpr int16_t kNavButtonWidth = LCD_WIDTH / 5;
inline constexpr int16_t kNavButtonHeight = kBottomBarHeight;
inline constexpr int16_t kNavButtonY = LCD_HEIGHT - kBottomBarHeight;
inline constexpr int16_t kNavButtonGap = 0;
inline constexpr int16_t kNavButtonStartX = 0;

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
        static_cast<int16_t>(kScreenMarginX + static_cast<int16_t>(index % kMainMenuColumns) * (kMainMenuCardWidth + kScreenGap)),
        static_cast<int16_t>(kContentTopY + static_cast<int16_t>(index / kMainMenuColumns) * (kMainMenuCardHeight + kScreenGap)),
        kMainMenuCardWidth,
        kMainMenuCardHeight,
    };
}

inline constexpr UiRect kFilesListPanelRect{ 0, kContentTopY, 320, kContentHeight };
inline constexpr UiRect kFilesDetailsPanelRect{
    static_cast<int16_t>(kFilesListPanelRect.w),
    kContentTopY,
    static_cast<int16_t>(LCD_WIDTH - kFilesListPanelRect.w),
    kContentHeight,
};
inline constexpr int16_t kFilesRunButtonWidth = 70;
inline constexpr int16_t kFilesRunButtonHeight = 28;
inline constexpr UiRect kFilesRunButtonRect{
    static_cast<int16_t>(kFilesDetailsPanelRect.x + (kFilesDetailsPanelRect.w - kFilesRunButtonWidth) / 2),
    static_cast<int16_t>(kFilesDetailsPanelRect.y + kFilesDetailsPanelRect.h - kFilesRunButtonHeight - kPanelBodyInsetY),
    kFilesRunButtonWidth,
    kFilesRunButtonHeight,
};
inline constexpr int16_t kFilesRowHeight = 30;
inline constexpr int16_t kFilesRowInsetX = kPanelBodyInsetX;
inline constexpr int16_t kFilesRowHeightInner = 26;
inline constexpr int16_t kFilesListHeaderHeight = kPanelHeaderHeight;

inline constexpr UiRect panel_body_rect(const UiRect& rect, int16_t header_height) {
    return UiRect{
        static_cast<int16_t>(rect.x + 1),
        static_cast<int16_t>(rect.y + header_height + 2),
        static_cast<int16_t>(rect.w - 2),
        static_cast<int16_t>(rect.h - header_height - 3),
    };
}

inline constexpr int16_t panel_text_x(const UiRect& rect) {
    return static_cast<int16_t>(rect.x + kPanelBodyInsetX);
}

inline constexpr int16_t panel_text_top(const UiRect& rect, int16_t header_height) {
    return static_cast<int16_t>(panel_body_rect(rect, header_height).y + kPanelBodyInsetY);
}

inline constexpr UiRect files_list_body_rect() {
    return panel_body_rect(kFilesListPanelRect, kFilesListHeaderHeight);
}

inline constexpr UiRect files_row_rect(int16_t index) {
    return UiRect{
        static_cast<int16_t>(kFilesListPanelRect.x + kFilesRowInsetX),
        static_cast<int16_t>(panel_body_rect(kFilesListPanelRect, kFilesListHeaderHeight).y + 4 + index * kFilesRowHeight),
        static_cast<int16_t>(kFilesListPanelRect.w - (kFilesRowInsetX * 2)),
        kFilesRowHeightInner,
    };
}
}  // namespace UiLayout
