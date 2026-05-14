#include "pico/stdlib.h"

#include "app/portable_cnc_app.h"
#include "calibration/calibration_storage.h"
#include "drivers/ili9488.h"
#include "drivers/sd_spi_card.h"
#include "drivers/xpt2046.h"

int main() {
    stdio_init_all();

    static Ili9488 display(spi0);
    display.init();

    static Xpt2046 touch(spi0);
    touch.init();
    touch.set_rotation(1);

    static SdSpiCard sd_card(spi1);
    sd_card.init();

    static CalibrationStorage storage;
    static PortableCncApp app(display, touch, sd_card, storage);
    app.run();
}
