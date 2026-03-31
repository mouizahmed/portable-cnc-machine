#pragma once

#include "app/status/status_line_formatter.h"
#include "drivers/ili9488.h"
#include "ui/components/ui_shell_types.h"

class StatusBar {
public:
    explicit StatusBar(Ili9488& display);

    void render(const StatusSnapshot& status) const;

private:
    Ili9488& display_;
    StatusLineFormatter formatter_;
};
