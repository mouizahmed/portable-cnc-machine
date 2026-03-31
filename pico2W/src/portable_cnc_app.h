#pragma once

#include "app_frame.h"
#include "calibration_storage.h"
#include "main_menu_screen.h"
#include "touch_calibration_app.h"

class PortableCncApp {
public:
    PortableCncApp(Ili9488& display, Xpt2046& touch, CalibrationStorage& storage);

    void run();

private:
    TouchCalibrationApp calibration_app_;
    AppFrame frame_;
    MainMenuScreen main_menu_screen_;
};
