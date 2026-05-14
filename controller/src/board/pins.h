#pragma once

/*
  Product-level Teensy 4.1 pin ownership.

  This file owns the full controller pin map. The grblHAL board map mirrors
  only the pins needed by grblHAL.
*/

// Motion pins intentionally avoid the TFT/touch SPI pins.
#define CNC_PIN_X_STEP              22
#define CNC_PIN_X_DIRECTION         23
#define CNC_PIN_X_ENABLE            24

#define CNC_PIN_Y_STEP              25
#define CNC_PIN_Y_DIRECTION         26
#define CNC_PIN_Y_ENABLE            27

#define CNC_PIN_Z_STEP              28
#define CNC_PIN_Z_DIRECTION         29
#define CNC_PIN_Z_ENABLE            30

#define CNC_PIN_X_LIMIT             31
#define CNC_PIN_Y_LIMIT             32
#define CNC_PIN_Z_LIMIT             33

// TFT/touch wiring from TEENSY_TFT_WIRING.md.
#define CNC_PIN_TFT_CS              10
#define CNC_PIN_TFT_RESET           9
#define CNC_PIN_TFT_DC              8
#define CNC_PIN_TFT_MOSI            11
#define CNC_PIN_TFT_SCK             13
#define CNC_PIN_TFT_LED             -1  // tied to 3.3V
#define CNC_PIN_TFT_MISO            -1  // display SDO intentionally not connected

#define CNC_PIN_TOUCH_CLK           13
#define CNC_PIN_TOUCH_CS            4
#define CNC_PIN_TOUCH_DIN           11
#define CNC_PIN_TOUCH_DO            12
#define CNC_PIN_TOUCH_IRQ           2

// Direct-to-Teensy inputs: assign after final hardware pin pass.
#define CNC_PIN_ESTOP               -1
#define CNC_PIN_PROBE               34
#define CNC_PIN_FEED_HOLD           -1
#define CNC_PIN_CYCLE_START         -1
#define CNC_PIN_RESET_BUTTON        -1
