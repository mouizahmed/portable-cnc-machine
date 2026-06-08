# PSU Power Sense Plan

## Goal

Add a 24V PSU power-present input so the Teensy can tell whether machine power is on. This lets firmware distinguish a real driver alarm from the motor supply simply being turned off while the Teensy is still powered over USB or logic power.

## Current Wiring

The PSU power-sense optocoupler is wired to Teensy pin 31.

Input side:

```text
24V PSU -> 4.7k resistor -> LTV LED anode
LTV LED cathode -> 24V PSU -
```

Teensy side:

```text
LTV emitter -> Teensy/buck GND
LTV collector -> Teensy pin 31
Teensy pin 31 -> 10k pullup -> 3.3V
```

Signal logic:

```text
24V present -> optocoupler on  -> pin reads LOW
24V off     -> optocoupler off -> pin reads HIGH
```

## Pin Conflict

Pin 31 was previously assigned as the X limit input:

```c
#define CNC_PIN_X_LIMIT 31
```

The firmware now reserves pin 31 for PSU power sense and moves X limit to pin 33:

```c
#define CNC_PIN_X_LIMIT 33
#define CNC_PIN_PSU_POWER_SENSE 31
```

## Firmware Plan

1. Add a named pin constant in `controller/src/board/pins.h`.

```c
#define CNC_PIN_PSU_POWER_SENSE 31
```

2. Keep X limit on pin 33 and PSU power sense on pin 31.

3. Add a small app-level monitor module.

```text
controller/src/app/app_power_sense.c
controller/src/app/app_power_sense.h
```

4. Configure the input with pullup.

```c
pinMode(CNC_PIN_PSU_POWER_SENSE, INPUT_PULLUP);
```

5. Interpret the signal as active-low.

```text
LOW  = machine 24V present
HIGH = machine 24V off/lost
```

6. Expose a function for other firmware modules.

```c
bool app_power_sense_machine_power_present(void);
```

7. Use this state in alarm handling.

```text
If machine power is present:
  monitor driver alarms normally.

If machine power is not present:
  suppress driver alarm faults caused by powered-off drivers.
  report/display machine power off instead.
```

8. Optionally send machine-power state to the desktop app as a status/event field.

## Test Plan

1. With 24V off, verify the input reads HIGH.
2. With 24V on, verify the input reads LOW.
3. Confirm driver alarm logic does not trigger when 24V is off.
4. Confirm real driver alarms still trigger when 24V is on.
5. Confirm X limit works on pin 33.
