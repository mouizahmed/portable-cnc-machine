# Portable CNC Machine — Pico 2W Wiring Reference

## Power

| Device | Device Pin | Pico Pin |
|---|---|---|
| Screen | VCC | 36 (3V3) |
| SD Breakout | VCC | 36 (3V3) |
| Screen | GND | 38 (GND) |
| SD Breakout | GND | 38 (GND) |
| Teensy 4.1 | GND | 38 (GND) — shared ground |

---

## Display + Touch (ILI9488 + XPT2046) — SPI0

| Signal | Pico GPIO | Pico Pin | LCD Pin |
|---|---|---|---|
| SCK | GP6 | 9 | SCK / T_CLK |
| MOSI | GP7 | 10 | MOSI / T_DIN |
| MISO | GP4 | 6 | T_DO (touch only — do NOT connect display SDO/MISO, it will pull this line to GND) |
| Display CS | GP5 | 7 | CS |
| Display DC | GP3 | 5 | DC |
| Display RST | GP2 | 4 | RST |
| Backlight | GP8 | 11 | LED |
| Touch CS | GP9 | 12 | T_CS |
| Touch IRQ | GP10 | 14 | T_IRQ |

---

## SD Card (Adafruit 4682 MicroSD SPI Breakout) — SPI0 shared

| SD Pin | Pico GPIO | Pico Pin | Notes |
|---|---|---|---|
| CLK | GP6 | 9 | Shared with display |
| MOSI | GP7 | 10 | Shared with display |
| MISO | GP4 | 6 | Shared with display |
| CS | GP13 | 17 | SD chip select |
| CD | GP14 | 19 | Card detect (optional) |

---

## Teensy 4.1 — UART

| Signal | Pico GPIO | Pico Pin | Teensy Pin | Notes |
|---|---|---|---|---|
| TX → Teensy RX | GP0 | 1 | 1 (RX1) | Pico → Teensy |
| RX ← Teensy TX | GP1 | 2 | 0 (TX1) | Teensy → Pico |
| Shared GND | GND | 38 | GND | Required |

Both Pico and Teensy 4.1 are 3.3V logic — direct connection, no level shifter needed.

---

## E-STOP Button — Physical

| Signal | Pico GPIO | Pico Pin | Notes |
|---|---|---|---|
| Button leg A | GP15 | 20 | Active-low, PULL_UP enabled in firmware |
| Button leg B | GND | any GND | |

---

## Full Pin Assignment Summary

```
GP0  → Teensy RX1
GP1  ← Teensy TX1
GP2  → Display RST
GP3  → Display DC
GP4  → SPI0 MISO    (display + touch + SD, shared)
GP5  → Display CS
GP6  → SPI0 SCK     (display + touch + SD, shared)
GP7  → SPI0 MOSI    (display + touch + SD, shared)
GP8  → Backlight
GP9  → Touch CS
GP10 → Touch IRQ
GP13 → SD CS
GP14 → SD CD        (card detect, optional)
GP15 → E-STOP button

GP11, GP12, GP16–GP22, GP26–GP28 → free for future use
```

---

## SPI Baudrate Notes

The SPI0 bus is shared between three devices. Firmware manages baudrate per transaction:

| Device | Baudrate |
|---|---|
| Display (ILI9488) | 40 MHz |
| Touch (XPT2046) | 2 MHz |
| SD Card | 25 MHz (max for FatFs) |

Each device has its own CS pin. Only one device is active at a time.
