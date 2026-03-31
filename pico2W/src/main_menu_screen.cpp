#include "main_menu_screen.h"

#include "config.h"

namespace {
constexpr uint16_t kColorCard = rgb565(30, 40, 50);
constexpr uint16_t kColorCardAccent = rgb565(38, 52, 66);
constexpr uint16_t kColorCardTitle = rgb565(246, 248, 252);
constexpr int16_t kCardTitleTop = 14;
constexpr int16_t kCardSubtitleTop = 40;
}  // namespace

const std::array<MainMenuScreen::MenuCard, 6> MainMenuScreen::kCards{{
    {24, 102, 136, 60, "RUN JOB", "START / RESUME"},
    {172, 102, 136, 60, "JOG", "MOVE AXES"},
    {320, 102, 136, 60, "ZERO/HOME", "WORK OFFSETS"},
    {24, 178, 136, 60, "FILES", "LOAD G-CODE"},
    {172, 178, 136, 60, "PROBE/TOOL", "TOUCH-OFF SETUP"},
    {320, 178, 136, 60, "SETTINGS", "CONFIG / CALIB"},
}};

MainMenuScreen::MainMenuScreen(Ili9488& display, AppFrame& frame)
    : display_(display), frame_(frame) {}

void MainMenuScreen::render() const {
    const StatusSnapshot status{
        "OK",
        "--",
        "--",
        "--",
        "--",
        "-- -- --",
        "12:34",
    };

    frame_.render_chrome(status, NavTab::Home);
    frame_.draw_screen_title("PORTABLE CNC", "MAIN MENU / MACHINE IDLE");

    for (const MenuCard& card : kCards) {
        draw_card(card);
    }

    frame_.draw_footer_status("READY");
}

void MainMenuScreen::draw_card(const MenuCard& card) const {
    display_.fill_rect(card.x, card.y, card.w, card.h, kColorCard);
    display_.draw_rect(card.x, card.y, card.w, card.h, COLOR_BORDER);
    display_.fill_rect(card.x + 1, card.y + 1, card.w - 2, 10, kColorCardAccent);

    const int16_t title_x = static_cast<int16_t>(card.x + (card.w - display_.text_width(card.title, 2)) / 2);
    const int16_t subtitle_x = static_cast<int16_t>(card.x + (card.w - display_.text_width(card.subtitle, 1)) / 2);

    display_.draw_text(title_x, static_cast<int16_t>(card.y + kCardTitleTop), card.title, kColorCardTitle, kColorCard, 2);
    display_.draw_text(subtitle_x, static_cast<int16_t>(card.y + kCardSubtitleTop), card.subtitle, COLOR_MUTED, kColorCard, 1);
}
