#pragma once

#include <cstdint>

#include "drivers/ili9488.h"
#include "drivers/xpt2046.h"
#include "ui/helpers/ui_helpers.h"

class UploadScreen {
public:
    explicit UploadScreen(Ili9488& display);

    void render(const char* name) const;

    bool hit_test_abort(const TouchPoint& point) const;

private:
    Ili9488& display_;
    UiPainter painter_;

    static constexpr int16_t kAbortW = 140;
    static constexpr int16_t kAbortH = 32;
    static const UiRect       kAbortRect;
    static const UiButtonStyle kAbortStyle;
};
