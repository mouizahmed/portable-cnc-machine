#pragma once

#include "drivers/ili9488.h"
#include "ui/helpers/ui_helpers.h"

struct SelectableListRowSpec {
    UiRect rect;
    const char* primary_text;
    const char* secondary_text;
    bool selected;
};

class SelectableListRow {
public:
    explicit SelectableListRow(Ili9488& display);

    void render(const SelectableListRowSpec& spec) const;

private:
    Ili9488& display_;
};
