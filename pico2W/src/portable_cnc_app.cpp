#include "portable_cnc_app.h"

#include "config.h"
#include "pico/stdlib.h"

PortableCncApp::PortableCncApp(Ili9488& display, Xpt2046& touch, CalibrationStorage& storage)
    : calibration_app_(display, touch, storage),
      frame_(display),
      main_menu_screen_(display, frame_) {}

void PortableCncApp::run() {
    TouchCalibration calibration{};
    calibration_app_.ensure_calibration(calibration);

    main_menu_screen_.render();

    while (true) {
        sleep_ms(UI_POLL_MS);
    }
}
