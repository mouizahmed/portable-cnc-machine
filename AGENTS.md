# Repository Guidelines

## Project Structure & Module Organization

This repository has two active software targets. `controller/` contains the Teensy 4.1 PlatformIO firmware; most edits belong under `controller/src/`, with app logic in `app/`, board settings in `board/`, protocol code in `protocol/`, storage in `storage/`, and UI in `ui/`. The grblHAL integration lives under `controller/src/grblhal/`; keep local changes minimal.

`desktop/` contains the Avalonia/.NET operator app. UI files are in `Views/` and `Controls/`, MVVM state is in `ViewModels/`, hardware and persistence code is in `Services/`, protocol definitions are in `Protocol/`, and OpenGL preview code is in `Rendering/`. Shared images and samples live in `assets/` and `desktop/samples/`. `reference/` is historical code.

## Build, Test, and Development Commands

- `dotnet build desktop/desktop.csproj` builds the desktop app.
- `dotnet run --project desktop/desktop.csproj` runs the Avalonia app locally.
- `pio run -d controller` builds the Teensy firmware.
- `pio test -d controller` runs PlatformIO tests when tests are present.
- `pio device monitor -d controller -b 250000` opens the controller serial monitor.

Use `PICO_WINDOWS_BUILD.txt` only for older Pico reference work.

## Coding Style & Naming Conventions

Match the surrounding style. Firmware code is C/C++ with snake_case filenames in module pairs such as `app_job_service.c` and `.h`. Keep board constants in `controller/src/board/` and protocol constants mirrored between `controller/src/protocol/` and `desktop/Protocol/`.

Desktop code uses C# nullable reference types and implicit usings. Use PascalCase for types, properties, and methods; camelCase for locals and private fields where established. Keep XAML views thin.

## Testing Guidelines

There is no broad automated test suite yet. For firmware changes, add PlatformIO tests under `controller/test/` when practical and run `pio run -d controller`. For desktop changes, run `dotnet build desktop/desktop.csproj`. For protocol changes, verify both sides stay byte-compatible.

## Commit & Pull Request Guidelines

Recent history uses short imperative commit subjects, for example `z brake control` and `remove stubs`. Keep subjects concise and specific; add a body when hardware behavior or protocol compatibility needs context.

Pull requests should describe the affected target (`controller`, `desktop`, or both), summarize validation commands, link related issues, and include screenshots or recordings for UI changes. Note required hardware setup or serial-port assumptions.

## Security & Configuration Tips

Do not commit generated build output from `.pio/`, `bin/`, `obj/`, `.build/`, or `desktop/artifacts/`. Avoid hard-coding machine-specific serial ports or calibration values; keep configurable behavior in settings or board/config modules.
