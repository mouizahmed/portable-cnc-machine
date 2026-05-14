/*
  my_machine_map.h - custom pin map for Teensy 4.1

  Part of grblHAL
*/

#define BOARD_NAME "My Machine"
#define BOARD_URL "local"

#if N_ABC_MOTORS > 0
#error "Custom map currently supports 3 axes only."
#endif

#if SPINDLE_SYNC_ENABLE
#error "Spindle sync is not supported for this custom map."
#endif

// Step pins
#define X_STEP_PIN          (2u)
#define Y_STEP_PIN          (5u)
#define Z_STEP_PIN          (8u)

// Direction pins
#define X_DIRECTION_PIN     (3u)
#define Y_DIRECTION_PIN     (6u)
#define Z_DIRECTION_PIN     (9u)

// Individual enable pins
#define X_ENABLE_PIN        (4u)
#define Y_ENABLE_PIN        (7u)
#define Z_ENABLE_PIN        (10u)

// Limit pins, matching the generic Teensy map for now
#define X_LIMIT_PIN         (20u)
#define Y_LIMIT_PIN         (21u)
#define Z_LIMIT_PIN         (22u)

// Auxiliary outputs
#define AUXOUTPUT0_PIN      (13u) // Spindle PWM
#define AUXOUTPUT1_PIN      (11u) // Spindle direction
#define AUXOUTPUT2_PIN      (12u) // Spindle enable
#define AUXOUTPUT3_PIN      (19u) // Coolant flood
#define AUXOUTPUT4_PIN      (18u) // Coolant mist

#if DRIVER_SPINDLE_ENABLE & SPINDLE_ENA
#define SPINDLE_ENABLE_PIN      AUXOUTPUT2_PIN
#endif
#if DRIVER_SPINDLE_ENABLE & SPINDLE_PWM
#define SPINDLE_PWM_PIN         AUXOUTPUT0_PIN
#endif
#if DRIVER_SPINDLE_ENABLE & SPINDLE_DIR
#define SPINDLE_DIRECTION_PIN   AUXOUTPUT1_PIN
#endif

#if COOLANT_ENABLE & COOLANT_FLOOD
#define COOLANT_FLOOD_PIN       AUXOUTPUT3_PIN
#endif
#if COOLANT_ENABLE & COOLANT_MIST
#define COOLANT_MIST_PIN        AUXOUTPUT4_PIN
#endif
