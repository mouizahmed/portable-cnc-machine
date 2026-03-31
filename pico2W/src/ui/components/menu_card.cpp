#include "ui/components/menu_card.h"

#include "config.h"

namespace {
constexpr uint16_t kColorCard = rgb565(30, 40, 50);
constexpr uint16_t kColorCardAccent = rgb565(38, 52, 66);
constexpr uint16_t kColorCardTitle = rgb565(246, 248, 252);
constexpr int16_t kCardTitleTop = 14;
constexpr int16_t kCardSubtitleTop = 40;
}  // namespace

MenuCard::MenuCard(Ili9488& display) : display_(display) {}

void MenuCard::render(const MenuCardSpec& card) const {
    const uint16_t fill = card.enabled ? kColorCard : rgb565(34, 38, 44);
    const uint16_t title_color = card.enabled ? kColorCardTitle : COLOR_MUTED;
    const uint16_t subtitle_color = card.enabled ? COLOR_MUTED : rgb565(122, 132, 142);

    display_.fill_rect(card.rect.x, card.rect.y, card.rect.w, card.rect.h, fill);
    display_.draw_rect(card.rect.x, card.rect.y, card.rect.w, card.rect.h, COLOR_BORDER);
    display_.fill_rect(card.rect.x + 1, card.rect.y + 1, card.rect.w - 2, 10, kColorCardAccent);

    const int16_t title_x = static_cast<int16_t>(card.rect.x + (card.rect.w - display_.text_width(card.title, 2)) / 2);
    const int16_t subtitle_x = static_cast<int16_t>(card.rect.x + (card.rect.w - display_.text_width(card.subtitle, 1)) / 2);

    display_.draw_text(title_x, static_cast<int16_t>(card.rect.y + kCardTitleTop), card.title, title_color, fill, 2);
    display_.draw_text(subtitle_x, static_cast<int16_t>(card.rect.y + kCardSubtitleTop), card.subtitle, subtitle_color, fill, 1);
}
