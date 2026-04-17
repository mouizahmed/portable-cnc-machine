#pragma once

#include <cstdint>

#include "drivers/ili9488.h"
#include "drivers/xpt2046.h"
#include "ui/helpers/ui_helpers.h"

class UploadScreen {
public:
    explicit UploadScreen(Ili9488& display);

    // Full redraw — call when entering the upload state
    void render(uint32_t bytes, uint32_t total, const char* name) const;

    // Partial update — call on each main-loop tick while uploading
    void render_progress(uint32_t bytes, uint32_t total) const;

    bool hit_test_abort(const TouchPoint& point) const;

private:
    Ili9488& display_;
    UiPainter painter_;

    static const UiRect      kAbortRect;
    static const UiButtonStyle kAbortStyle;
};
