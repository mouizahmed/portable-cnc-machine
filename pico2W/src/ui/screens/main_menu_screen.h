#pragma once

#include <array>
#include <cstdint>

#include "ui/components/app_frame.h"
#include "ui/components/menu_card.h"
#include "ui/screens/screen.h"

class MainMenuScreen : public Screen {
public:
    MainMenuScreen(Ili9488& display, AppFrame& frame);

    NavTab tab() const override;
    void render(const StatusSnapshot& status) override;
    UiEventResult handle_event(const UiEvent& event) override;

private:
    static const std::array<MenuCardSpec, 6> kCards;

    AppFrame& frame_;
    MenuCard menu_card_;
};
