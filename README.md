<div align="center">
    <img alt="Logo" src="assets/logo.png" width="100" />
</div>
<h1 align="center">Portable CNC Machine</h1>
<h3 align="center">Capstone Team 40 — Team XYZ</h3>
<p align="center">York University — Lassonde School of Engineering</p>
<p align="center">2025–2026</p>

The Pico firmware now lives in `[pico2W](./pico2W)` as a Pico SDK C++ project with a simplified touchscreen UI.

## Hardware Overview

The project is split across a few hardware pieces:

- `Pico 2W`: runs the touchscreen UI and calibration utility in `[pico2W](./pico2W)`.
- `Teensy 4.1`: runs the machine-control firmware in `[teensy4.1](./teensy4.1)`.
- `3.5"` SPI display with `ILI9488` controller and `XPT2046` touch controller: connected to the Pico over shared SPI.
- `MicroSD` SPI breakout: shares the Pico SPI bus with the display and touch controller.
- `E-STOP` button: wired to the Pico as an active-low input.

Pin assignments and wiring details are documented in `[WIRING.md](./WIRING.md)`.

## License

This repository is licensed under the MIT License. See `[LICENSE](./LICENSE)` for the full text.