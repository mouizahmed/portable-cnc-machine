/*
  my_machine_map.h - portable CNC Teensy 4.1 pin map

  Project-owned grblHAL board map. Keep this file limited to the pins grblHAL
  needs; broader product pin ownership lives in src/board/pins.h.
*/

#pragma once

#include "board/pins.h"

#define BOARD_NAME "Portable CNC Teensy 4.1"
#define BOARD_URL  "local"

#if N_ABC_MOTORS > 0
#error "Portable CNC map currently supports 3 axes only."
#endif

#if SPINDLE_SYNC_ENABLE
#error "Spindle sync is not supported by the current Portable CNC pin map."
#endif

#define X_STEP_PIN          (CNC_PIN_X_STEP)
#define Y_STEP_PIN          (CNC_PIN_Y_STEP)
#define Z_STEP_PIN          (CNC_PIN_Z_STEP)

#define X_DIRECTION_PIN     (CNC_PIN_X_DIRECTION)
#define Y_DIRECTION_PIN     (CNC_PIN_Y_DIRECTION)
#define Z_DIRECTION_PIN     (CNC_PIN_Z_DIRECTION)

#define STEPPERS_ENABLE_PIN (CNC_PIN_STEPPERS_ENABLE)
#define Z_BRAKE_RELAY_PIN   (CNC_PIN_Z_BRAKE_RELAY)
#define Z_BRAKE_RELAY_ON    (CNC_PIN_Z_BRAKE_RELAY_ON)

#define X_LIMIT_PIN         (CNC_PIN_X_LIMIT)
#define Y_LIMIT_PIN         (CNC_PIN_Y_LIMIT)
#define Z_LIMIT_PIN         (CNC_PIN_Z_LIMIT)

#define X_LIMIT_PIN_MAX     (CNC_PIN_X_LIMIT_MAX)
#define Y_LIMIT_PIN_MAX     (CNC_PIN_Y_LIMIT_MAX)
#define Z_LIMIT_PIN_MAX     (CNC_PIN_Z_LIMIT_MAX)
