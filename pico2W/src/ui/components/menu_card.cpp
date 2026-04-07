#include "ui/components/menu_card.h"

#include "config.h"

namespace {
constexpr uint16_t kColorCard = rgb565(30, 40, 50);
constexpr uint16_t kColorCardAccent = rgb565(38, 52, 66);
constexpr uint16_t kColorCardTitle = rgb565(246, 248, 252);
constexpr int16_t kCardAccentHeight = 14;
}  // namespace

MenuCard::MenuCard(Ili9488& display) : display_(display) {}

void MenuCard::render(const MenuCardSpec& card) const {
    const uint16_t fill = card.enabled ? kColorCard : rgb565(34, 38, 44);
    const uint16_t title_color = card.enabled ? kColorCardTitle : COLOR_MUTED;
    const uint16_t subtitle_color = card.enabled ? COLOR_MUTED : COLOR_MUTED;
    const uint8_t title_scale = 2;
    const uint8_t subtitle_scale = 1;

    display_.fill_rect(card.rect.x, card.rect.y, card.rect.w, card.rect.h, fill);
    display_.draw_rect(card.rect.x, card.rect.y, card.rect.w, card.rect.h, COLOR_BORDER);
    display_.fill_rect(card.rect.x + 1, card.rect.y + 1, card.rect.w - 2, kCardAccentHeight, kColorCardAccent);

    const int16_t content_y = static_cast<int16_t>(card.rect.y + kCardAccentHeight);
    const int16_t content_h = static_cast<int16_t>(card.rect.h - kCardAccentHeight);
    const int16_t combined_h = static_cast<int16_t>(display_.text_height(title_scale) +
                                                    display_.text_height(subtitle_scale) + 10);
    const int16_t title_x = static_cast<int16_t>(card.rect.x + (card.rect.w - display_.text_width(card.title, title_scale)) / 2);
    const int16_t subtitle_x = static_cast<int16_t>(card.rect.x + (card.rect.w - display_.text_width(card.subtitle, subtitle_scale)) / 2);
    const int16_t title_y = static_cast<int16_t>(content_y + (content_h - combined_h) / 2);
    const int16_t subtitle_y = static_cast<int16_t>(title_y + display_.text_height(title_scale) + 10);

    display_.draw_text(title_x, title_y, card.title, title_color, fill, title_scale);
    display_.draw_text(subtitle_x, subtitle_y, card.subtitle, subtitle_color, fill, subtitle_scale);
}
