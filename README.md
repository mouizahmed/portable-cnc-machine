<div align="center">
    <img alt="Logo" src="assets/logo.png" width="100" />
    <h1>Portable CNC Machine</h1>
    <h3>Capstone Team 40 | Team XYZ</h3>
    <p>York University | Lassonde School of Engineering</p>
    <p>2025–2026</p>
</div>

A modular, portable 3-axis CNC vertical milling machine prototype designed for remote First Nations communities in Canada. Built to fit on a pickup truck bed, operate fully offline, and be maintained with basic hand tools, reducing dependence on distant suppliers for critical replacement parts.

## Team

| Name | Role | Student ID |
|------|------|------------|
| Mouiz Ahmed | Software Engineer | 218105536 |
| Farzin Aliverdi Mamaghani | Software Engineer | 217849068 |
| Vladislav Fedotov | Mechanical Engineer | 218130435 |
| Quoc Tri (Lloyd) Lam | Mechanical Engineer | 219012434 |
| Anastasia Vitkovskiy | Mechanical Engineer | 218644609 |
| Cameron Waters | Mechanical Engineer | 218127605 |

## Repository Structure

```
portable-cnc-machine/
├── pico2W/                  # Pico 2W firmware (C/C++, Pico SDK)
│   └── src/
│       ├── app/             # Application logic (job, jog, machine, navigation, status, storage)
│       ├── calibration/     # Touchscreen calibration app and storage
│       ├── core/            # Shared state types
│       ├── drivers/         # ILI9488 display, XPT2046 touch, SD card SPI drivers
│       ├── protocol/        # USB CDC transport and desktop protocol implementation
│       ├── services/        # CNC controller service (state machine)
│       └── ui/              # Screens, components, layout, helpers
├── teensy4.1/               # Teensy 4.1 firmware (C++, PlatformIO)
│   └── src/
│       ├── grbl/            # grblHAL core — G-code parser, motion planner, stepper, spindle, state machine
│       ├── boards/          # Pin mapping headers for supported board configurations
│       ├── littlefs/        # LittleFS embedded filesystem
│       ├── driver.c/h       # Teensy 4.1 HAL driver
│       ├── tmc_spi.c        # TMC2209 stepper driver SPI interface
│       ├── uart.c/h         # UART communication
│       ├── my_stream.cpp/h  # Custom stream handler (Pico serial passthrough)
│       └── my_machine.h     # Machine configuration
├── desktop/                 # Desktop GUI (C#, .NET/Avalonia)
│   ├── Controls/            # Reusable UI controls
│   ├── Models/              # Data models
│   ├── Rendering/           # OpenGL toolpath visualizer (shaders, camera, renderers)
│   ├── Services/            # Serial, USB device, G-code, Pico protocol, settings services
│   ├── ViewModels/          # MVVM view models (connect, dashboard, files, manual control, diagnostics)
│   ├── Views/               # Pages and dialogs
│   └── samples/             # Sample G-code files for testing
└── assets/                  # Project assets (logo, etc.)
```

## Software Architecture

<!-- TODO -->

## Hardware Architecture

<!-- TODO -->

## Getting Started

<!-- TODO -->

## Hardware Overview

The project is split across a few hardware pieces:

- `Pico 2W`: runs the touchscreen UI and calibration utility in `[pico2W](./pico2W)`.
- `Teensy 4.1`: runs the machine-control firmware in `[teensy4.1](./teensy4.1)`.
- `3.5"` SPI display with `ILI9488` controller and `XPT2046` touch controller: connected to the Pico over shared SPI.
- `MicroSD` SPI breakout: shares the Pico SPI bus with the display and touch controller.
- `E-STOP` button: wired to the Pico as an active-low input.

Pin assignments and wiring details are documented in `[WIRING.md](./WIRING.md)`.

## License

This repository is licensed under the MIT License. See [LICENSE](./LICENSE) for the full text.