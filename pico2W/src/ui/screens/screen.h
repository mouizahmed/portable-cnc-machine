#pragma once

#include "ui/components/ui_shell_types.h"
#include "ui/ui_event.h"

class Screen {
public:
    virtual ~Screen() = default;

    virtual NavTab tab() const = 0;
    virtual void render(const StatusSnapshot& status) = 0;
    virtual void render_content() = 0;
    virtual UiEventResult handle_event(const UiEvent& event) = 0;
};
