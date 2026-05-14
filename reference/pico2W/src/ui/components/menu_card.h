#pragma once

#include "ui/components/ui_shell_types.h"
#include "drivers/ili9488.h"
#include "ui/helpers/ui_helpers.h"

struct MenuCardSpec {
    UiRect rect;
    const char* title;
    const char* subtitle;
    NavTab target_tab;
    bool enabled;
};

class MenuCard {
public:
    explicit MenuCard(Ili9488& display);

    void render(const MenuCardSpec& card) const;

private:
    Ili9488& display_;
};
