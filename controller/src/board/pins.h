#pragma once

/*
  Product-level Teensy 4.1 pin ownership.

  This file owns the full controller pin map. The grblHAL board map mirrors
  only the pins needed by grblHAL.
*/

// Motion pins intentionally avoid the TFT/touch SPI pins.
#define CNC_PIN_X_STEP              24
#define CNC_PIN_X_DIRECTION         25
#define CNC_PIN_STEPPERS_ENABLE     30

#define CNC_PIN_Y_STEP              26
#define CNC_PIN_Y_DIRECTION         27

#define CNC_PIN_Z_STEP              28
#define CNC_PIN_Z_DIRECTION         29
#define CNC_PIN_Z_BRAKE_RELAY       35

// PN2222 relay driver: Teensy high turns transistor on, energizing active-low relay IN.
#define CNC_PIN_Z_BRAKE_RELAY_ON    1

// Electronics box cooling fan. Active high.
#define CNC_PIN_FAN                 31
#define CNC_PIN_FAN_ON              1

// grblHAL's Teensy driver requires limit pins to be defined. These are
// placeholders until final limit wiring is assigned.
#define CNC_PIN_X_LIMIT             33
#define CNC_PIN_Y_LIMIT             40
#define CNC_PIN_Z_LIMIT             41
#define CNC_PIN_X_LIMIT_MAX         14
#define CNC_PIN_Y_LIMIT_MAX         15
#define CNC_PIN_Z_LIMIT_MAX         16
#define CNC_PIN_Z_ALM               32

// Temporary encoder test input. These pins are currently unused by the
// product pin map and are safe for interrupt-based quadrature testing.
#define CNC_PIN_ENCODER0_A          2
#define CNC_PIN_ENCODER0_B          3

#define CNC_PIN_TFT_CS              10
#define CNC_PIN_TFT_RESET           37
#define CNC_PIN_TFT_DC              36
#define CNC_PIN_TFT_MOSI            11
#define CNC_PIN_TFT_SCK             13
#define CNC_PIN_TFT_LED             -1  // tied to 3.3V
#define CNC_PIN_TFT_MISO            -1  // display SDO intentionally not connected

#define CNC_PIN_TOUCH_CLK           13
#define CNC_PIN_TOUCH_CS            38
#define CNC_PIN_TOUCH_DIN           11
#define CNC_PIN_TOUCH_DO            12
#define CNC_PIN_TOUCH_IRQ           39

// BMP280 ambient sensor uses Teensy Wire I2C0.
#define CNC_PIN_BMP280_SDA          18
#define CNC_PIN_BMP280_SCL          19

// Direct-to-Teensy inputs: assign after final hardware pin pass.
#define CNC_PIN_ESTOP               -1
#define CNC_PIN_PROBE               34
#define CNC_PIN_FEED_HOLD           -1
#define CNC_PIN_CYCLE_START         -1
#define CNC_PIN_RESET_BUTTON        -1
