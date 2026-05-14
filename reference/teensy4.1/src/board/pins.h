#pragma once

/*
  Product-level Teensy 4.1 pin ownership.

  This file is the whole-controller pin inventory. grblHAL consumes only the
  subset mirrored in grblhal/boards/my_machine_map.h.

  NOTE: Current TFT assignments conflict with the active motion/spindle map.
  Resolve these before enabling TFT firmware.
*/

// Motion pins currently mirrored by grblhal/boards/my_machine_map.h.
#define CNC_PIN_X_STEP              2
#define CNC_PIN_X_DIRECTION         3
#define CNC_PIN_X_ENABLE            4

#define CNC_PIN_Y_STEP              5
#define CNC_PIN_Y_DIRECTION         6
#define CNC_PIN_Y_ENABLE            7

#define CNC_PIN_Z_STEP              8
#define CNC_PIN_Z_DIRECTION         9
#define CNC_PIN_Z_ENABLE            10

#define CNC_PIN_X_LIMIT             20
#define CNC_PIN_Y_LIMIT             21
#define CNC_PIN_Z_LIMIT             22

// Spindle and coolant pins currently mirrored by grblhal/boards/my_machine_map.h.
#define CNC_PIN_SPINDLE_PWM         13
#define CNC_PIN_SPINDLE_DIRECTION   11
#define CNC_PIN_SPINDLE_ENABLE      12
#define CNC_PIN_COOLANT_FLOOD       19
#define CNC_PIN_COOLANT_MIST        18

// TFT/touch wiring documented in TEENSY_TFT_WIRING.md.
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

// Planned direct-to-Teensy inputs. Assign after resolving pin conflicts.
#define CNC_PIN_ESTOP               -1
#define CNC_PIN_PROBE               -1
#define CNC_PIN_FEED_HOLD           -1
#define CNC_PIN_CYCLE_START         -1
#define CNC_PIN_RESET_BUTTON        -1
