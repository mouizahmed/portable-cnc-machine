<h1>
  <img src="assets/logo.png" alt="Portable CNC Machine logo" width="80" align="center">
  Portable CNC Machine
</h1>

**Capstone Team 40 &mdash; Team XYZ (2025-2026)**  
York University | Lassonde School of Engineering

---

A modular, portable 3-axis CNC vertical milling machine designed for remote First Nations communities in Canada. The system is built to fit in a pickup truck bed, operate fully offline, and support field maintenance with basic hand tools, helping communities produce critical replacement parts without relying on distant suppliers.

The machine combines a rigid mechanical frame that can fully disassemble into parts <= 25 kg, 3-axis stepper-driven motion hardware with encoder-based position verification, a Teensy 4.1 grblHAL motion controller, an onboard touchscreen interface, SD-card job storage, and an Avalonia/.NET desktop application for G-code preview, file transfer, machine control, and diagnostics.

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
├── assets/                  # Project assets, including the README logo
├── controller/              # Active Teensy 4.1 controller firmware
│   ├── include/             # Shared firmware headers
│   ├── lib/                 # PlatformIO libraries
│   ├── src/                 # Firmware source
│   │   ├── app/             # Application-level controller logic
│   │   ├── app_config/      # Runtime/configuration definitions
│   │   ├── board/           # Board-specific setup
│   │   ├── grblhal/         # grblHAL integration
│   │   ├── machine/         # Machine control and state
│   │   ├── protocol/        # Host/controller protocol
│   │   ├── storage/         # Local storage support
│   │   ├── ui/              # Controller UI code
│   │   └── main.cpp         # Firmware entry point
│   ├── test/                # PlatformIO tests
│   └── platformio.ini       # Controller build configuration
├── desktop/                 # Desktop GUI (C#, .NET/Avalonia)
│   ├── Assets/              # Desktop application assets
│   ├── Controls/            # Reusable UI controls
│   ├── Models/              # Data models
│   ├── Protocol/            # Binary protocol definitions and helpers
│   ├── Rendering/           # OpenGL toolpath visualizer
│   ├── Services/            # Serial, G-code, settings, and app services
│   ├── ViewModels/          # MVVM view models
│   ├── Views/               # Pages and dialogs
│   ├── samples/             # Sample G-code files
│   └── desktop.csproj       # Desktop app project file
├── docs/                    # Project documentation assets
│   └── uart/                # UART protocol documentation/support files
├── reference/               # Older firmware implementations kept for reference
│   ├── pico2W/              # Former Pico 2W firmware
│   └── teensy4.1/           # Former Teensy 4.1 firmware
├── LICENSE
├── PICO_WINDOWS_BUILD.txt
└── README.md
```

## Software Architecture

The system is split across two active software targets:

- **Desktop app** — Avalonia/.NET operator interface. It discovers the Teensy USB
  serial ports, sends machine/file/job commands through `ControllerProtocolService`,
  stores operator settings locally, parses G-code, and renders the toolpath with the
  native OpenGL previewer.
- **Teensy 4.1 controller firmware** — PlatformIO/Arduino firmware built around
  grblHAL. grblHAL owns real-time motion planning, G-code execution, limits, probing,
  spindle control, and machine settings. The project app layer plugs into grblHAL at
  startup and adds touchscreen UI, SD-card file handling, job streaming, machine-state
  snapshots, and the desktop control protocol.

Current communication paths:

- **grblHAL stream** — The first Teensy USB CDC serial interface remains available for
  normal grblHAL text traffic and direct controller diagnostics.
- **Desktop control protocol** — The second Teensy USB CDC serial interface
  (`SerialUSB1`) carries binary frames with COBS encoding, CRC32 validation, sequence
  numbers, and up to 4096-byte payloads. Frame types 1-7 cover upload data/ack,
  download data/ack, commands, responses, and events as defined in
  `controller/src/protocol/protocol_defs.h` and `desktop/Protocol/ProtocolDefs.cs`.
- **Storage and jobs** — Desktop file uploads/downloads, SD-card file operations, job
  load/start/pause/resume/abort, position updates, safety state, and settings exchange
  all flow through the framed desktop protocol.

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
