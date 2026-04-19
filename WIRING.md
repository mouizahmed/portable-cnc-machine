# Portable CNC Machine - Wiring Reference

This document includes the `pico2W` firmware assumptions and a Teensy 4.1 schematic pinout plan. Align `teensy4.1` `my_machine_map.h` with any hardware you build.

On Teensy 4.1 symbols, the physical pin is the red index (1-67). The Arduino digital pin is the leading number on the silk label.

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

## Teensy 4.1 - UART

These pins are the intended Pico-to-Teensy UART link.

| Signal | Pico GPIO | Pico Pin | Teensy physical pin | Teensy silk (Serial1) |
|---|---|---|---|---|
| TX -> Teensy RX | GP0 | 1 | 2 | `0_RX1...` (D0) |
| RX <- Teensy TX | GP1 | 2 | 3 | `1_TX1...` (D1) |
| Shared GND | GND | 38 | any GND | e.g. 1, 34, 47 |

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
GP0  -> Teensy RX1
GP1  <- Teensy TX1
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

---

## Teensy 4.1 - Schematic Pinout Plan

Use one row per net. Step/dir/enable go to driver `PUL / DIR / ENA` plus common GND. UART: Pico TX (`GP0`) -> Teensy D0 (RX), Pico RX (`GP1`) <- Teensy D1 (TX).

### Pico UART (Serial1)

| Physical pin | Silk / function | Net / use |
|---|---|---|
| 2 | `0_RX1...` (D0) | From Pico TX (GP0) |
| 3 | `1_TX1...` (D1) | To Pico RX (GP1) |

### Limits / home (example: D2-D4)

| Physical pin | Silk | Net |
|---|---|---|
| 4 | `2_OUT2` (D2) | X_HOME |
| 5 | `3_LRCLK2` (D3) | Y_HOME |
| 6 | `4_BCLK2` (D4) | Z_HOME |

### Probe and E-stop (example: D5, D6)

| Physical pin | Silk | Net |
|---|---|---|
| 7 | `5_IN2` (D5) | Z_PROBE |
| 8 | `6_OUT1D` (D6) | ESTOP_N (if wired to Teensy) |

### Encoders (example: D7-D12)

| Physical pin | Silk | Net |
|---|---|---|
| 9 | `7_RX2_OUT1A` (D7) | X_ENC_A |
| 10 | `8_TX2_IN1` (D8) | X_ENC_B |
| 11 | `9_OUT1C` (D9) | Y_ENC_A |
| 12 | `10_CS_MQSR` (D10) | Y_ENC_B |
| 13 | `11_MOSI_CTX1` (D11) | Z_ENC_A |
| 14 | `12_MISO_MQSL` (D12) | Z_ENC_B |

### Stepper Drivers (example: D15-D23)

These physical pin numbers match the Teensy 4.1 symbol. On the right row, each `N_A*` label sits one pin higher than a naive count from the bottom.

| Physical pin | Silk | Net |
|---|---|---|
| 45 | `23_A9_CRX1_MCLK1` (D23) | X_STEP |
| 44 | `22_A8_CTX1` (D22) | X_DIR |
| 43 | `21_A7_RX5_BCLK1` (D21) | X_EN |
| 42 | `20_A6_TX5_LRCLK1` (D20) | Y_STEP |
| 41 | `19_A5_SCL` (D19) | Y_DIR |
| 40 | `18_A4_SDA` (D18) | Y_EN |
| 39 | `17_A3_TX4_SDA1` (D17) | Z_STEP |
| 38 | `16_A2_RX4_SCL1` (D16) | Z_DIR |
| 37 | `15_A1_RX3_SPDIF_IN` (D15) | Z_EN |

### Spindle and Z Brake (example)

`14_A0...` (Arduino `D14`) is on physical pin 36. The next pin up the row (35) is `13_SCK_LED` (`D13`), not D14.

| Physical pin | Silk | Net |
|---|---|---|
| 36 | `14_A0_TX3_SPDIF_OUT` (D14) | SPINDLE_PWM |
| 33 | `41_A17` (D41) | Z_BRAKE_EN |

### Power / ground

| Physical pin | Label | Net |
|---|---|---|
| 48 | VIN | +5 V (or your regulated bus) |
| 47, 34 | GND | GND |
