#include "pico/stdlib.h"

#include "app/portable_cnc_app.h"
#include "calibration/calibration_storage.h"
#include "drivers/ili9488.h"
#include "drivers/xpt2046.h"

int main() {
    stdio_init_all();

    Ili9488 display(spi0);
    display.init();

    Xpt2046 touch(spi0);
    touch.init();
    touch.set_rotation(1);

    CalibrationStorage storage;
    PortableCncApp app(display, touch, storage);
    app.run();
}
