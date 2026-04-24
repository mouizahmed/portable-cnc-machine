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

The system is split across three cooperating runtimes:

- **Desktop app** — Avalonia/.NET operator interface. It talks to the Pico over USB CDC
  using binary COBS/CRC-framed CMD/RESP/EVENT packets and renders G-code locally with the
  native OpenGL previewer.
- **Pico 2W firmware** — Owns the touchscreen UI, SD card storage, job selection,
  binary desktop protocol, file transfers, safety/capability state, and Pico-to-Teensy
  motion-link supervision.
- **Teensy 4.1 firmware** — Runs grblHAL and owns real motion execution. The Pico sends
  newline-terminated ASCII motion commands over UART; the Teensy reports boot, GRBL state,
  position, and per-line G-code acceptance.

Current wire protocols:

- Desktop <-> Pico: binary frame types 1-7 as defined in `pico2W/src/protocol/protocol_defs.h`
  and `desktop/Protocol/ProtocolDefs.cs`.
- Pico <-> Teensy: ASCII UART lines such as `@BOOT TEENSY_READY`,
  `@GRBL_STATE IDLE`, `@POS MX=...`, `@HOME`, `@JOG ...`, `@GCODE ...`,
  `@RT_FEED_HOLD`, and bare `ok` / `error:<code>` responses.

## Hardware Architecture

<!-- TODO -->

## Getting Started

Build checks:

```sh
dotnet build desktop/desktop.csproj
cmake -S pico2W -B pico2W/build -DPICO_BOARD=pico2_w
cmake --build pico2W/build
pio run -d teensy4.1/src
```

Bring-up checklist:

1. Flash the Pico 2W UF2 from `pico2W/build`.
2. Flash the Teensy 4.1 firmware with PlatformIO.
3. Wire Pico `GP0` TX to Teensy RX1, Pico `GP1` RX to Teensy TX1, common GND, and active-low E-stop on Pico `GP15`.
4. Connect the desktop app to the Pico USB CDC port and verify Teensy connected state.
5. Upload/load a G-code file, run a dry job, test pause/resume/abort, and test E-stop/reset recovery.


## License

This repository is licensed under the MIT License. See [LICENSE](./LICENSE) for the full text.
