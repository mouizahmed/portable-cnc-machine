# Portable CNC Machine - Pico 2W Wiring Reference

This document reflects the current `pico2W` firmware wiring assumptions.

## Power

| Device | Device Pin | Pico Pin |
|---|---|---|
| Screen | VCC | 36 (3V3) |
| SD Breakout | VCC | 36 (3V3) |
| Screen | GND | 38 (GND) |
| SD Breakout | GND | 38 (GND) |
| Teensy 4.1 | GND | 38 (GND) - shared ground |

---

## Display + Touch (ILI9488 + XPT2046) - SPI0

These pins match the active Pico firmware in [`pico2W/src/config.h`](./pico2W/src/config.h).

| Signal | Pico GPIO | Pico Pin | Module Pin |
|---|---|---|---|
| SCK | GP6 | 9 | SCK / T_CLK |
| MOSI | GP7 | 10 | MOSI / T_DIN |
| MISO | GP4 | 6 | T_DO |
| Display CS | GP5 | 7 | CS |
| Display DC | GP3 | 5 | DC |
| Display RST | GP2 | 4 | RST |
| Backlight | GP8 | 11 | LED |
| Touch CS | GP9 | 12 | T_CS |
| Touch IRQ | GP10 | 14 | T_IRQ |

Note: do not connect the display SDO/MISO line on boards that pull MISO low. The current firmware expects the shared MISO line to be available for touch and SD.

---

## SD Card (SPI0 Shared)

The SD card shares `SPI0` with the display and touch controller.

| SD Pin | Pico GPIO | Pico Pin | Notes |
|---|---|---|---|
| CLK | GP6 | 9 | Shared with display and touch |
| MOSI | GP7 | 10 | Shared with display and touch |
| MISO | GP4 | 6 | Shared with display and touch |
| CS | GP11 | 15 | SD chip select used by current firmware |

Note: the current Pico firmware does not use a card-detect pin.

---

## Teensy 4.1 - UART (Planned / Reserved)

These pins are documented as the intended Pico-to-Teensy UART link, but the current Pico firmware does not initialize or use UART yet.

| Signal | Pico GPIO | Pico Pin | Teensy Pin | Notes |
|---|---|---|---|---|
| TX -> Teensy RX | GP0 | 1 | 1 (RX1) | Reserved for Pico -> Teensy |
| RX <- Teensy TX | GP1 | 2 | 0 (TX1) | Reserved for Teensy -> Pico |
| Shared GND | GND | 38 | GND | Required |

Both Pico and Teensy 4.1 are 3.3 V logic, so no level shifter should be needed for a direct UART connection.

---

## E-STOP Button (Reserved in Config)

`GP15` is defined as `PIN_ESTOP` in the Pico config, but the current firmware does not initialize or read this input yet.

| Signal | Pico GPIO | Pico Pin | Notes |
|---|---|---|---|
| Button leg A | GP15 | 20 | Reserved in config, not yet handled by firmware |
| Button leg B | GND | any GND | Active-low wiring intended |

---

## Full Pin Assignment Summary

```text
GP0  -> Reserved for Teensy UART TX
GP1  -> Reserved for Teensy UART RX
GP2  -> Display RST
GP3  -> Display DC
GP4  -> SPI0 MISO    (display + touch + SD, shared)
GP5  -> Display CS
GP6  -> SPI0 SCK     (display + touch + SD, shared)
GP7  -> SPI0 MOSI    (display + touch + SD, shared)
GP8  -> Backlight
GP9  -> Touch CS
GP10 -> Touch IRQ
GP11 -> SD CS
GP15 -> Reserved for E-STOP

GP12-GP14, GP16-GP22, GP26-GP28 -> currently free for future use
```

---

## SPI Baudrate Notes

The firmware changes SPI speed per device transaction on the shared `SPI0` bus:

| Device | Baudrate |
|---|---|
| Display (ILI9488) | 20 MHz |
| Touch (XPT2046) | 2 MHz |
| SD Card init | 400 kHz |
| SD Card normal | 12 MHz |

Each device has its own CS pin. Only one device is active at a time.
