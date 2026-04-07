#pragma once

#include <cstddef>
#include <cstdint>

#include "drivers/ili9488.h"
#include "ui/components/nav_bar.h"
#include "ui/components/status_bar.h"
#include "ui/components/ui_shell_types.h"
#include "ui/ui_event.h"

class AppFrame {
public:
    explicit AppFrame(Ili9488& display);

    void render_chrome(const StatusSnapshot& status, NavTab active_tab) const;
    void render_status_bar(const StatusSnapshot& status) const;
    void render_nav_bar(NavTab active_tab) const;
    void clear_content() const;
    void draw_footer_status(const char* status_text) const;
    UiEventResult handle_event(const UiEvent& event) const;

    int16_t content_top() const;
    int16_t content_bottom() const;

private:
    Ili9488& display_;
    StatusBar status_bar_;
    NavBar nav_bar_;
};
