# Desktop App — Implementation Plan

## Architecture Principle

The desktop is a **stateless viewer**. It receives `@STATE`, `@CAPS`, `@SAFETY`, `@EVENT`,
and `@POS` from the Pico and reflects them. It sends `@`-commands to the Pico. It holds
no machine state of its own.

All capability gating (what controls are enabled) comes from `@CAPS` flags. The desktop
never re-derives rules from state strings.

---

## What Stays

| Component | Reason |
|---|---|
| `SerialService` | Dumb transport, already clean |
| `GCodeParser` / `GCodeDocument` | Still needed for local visualization |
| Rendering stack (`NativeGlToolpathView`, `ToolpathGlRenderer`, etc.) | Unchanged |
| `DashboardViewModel` playback logic | Pure local UI state, no machine state |
| `SettingsService` / `AppSettings` | Unchanged |
| `ConnectionStatus` enum | Still needed for desktop→Pico link status |

---

## What Changes

| Component | Change |
|---|---|
| `Enums.cs` | Replace `MotionState`/`SafetyState` with new types |
| `MainWindowViewModel` | Remove GRBL parsing, become a pure `@`-protocol sink |
| `ConnectViewModel` | Replace `?`-polling handshake with `@PING`/`@INFO` |
| `ManualControlViewModel` | Remove optimistic state mutation, send `@`-commands |
| `FilesViewModel` | File list from protocol, remove direct state writes |

---

## New Types (`Enums.cs`)

```csharp
// 12-state unified FSM — mirrors STATE_MACHINE.md
public enum MachineOperationState
{
    Booting, TeensyDisconnected, Syncing,
    Idle, Homing, Jog, Starting, Running,
    Hold, Fault, Estop, CommsFault
}

// Orthogonal safety level
public enum SafetyLevel { Safe, Monitoring, Warning, Critical }

// Per-action capability flags — desktop binds these directly, no re-derivation
public record struct CapsFlags(
    bool Motion, bool Probe, bool Spindle, bool FileLoad,
    bool JobStart, bool JobPause, bool JobResume, bool JobAbort,
    bool Overrides, bool Reset);
```

---

## New Service (`PicoProtocolService`)

Wraps `SerialService`. Parses every incoming `@`-line into typed events.
Exposes typed `Send*()` methods for every outbound command.

**Incoming events:**
```csharp
event Action<MachineOperationState>                        StateChanged;
event Action<CapsFlags>                                    CapsChanged;
event Action<SafetyLevel>                                  SafetyChanged;
event Action<string, IReadOnlyDictionary<string, string>>  EventReceived;  // @EVENT name {kv}
event Action<PicoPos>                                      PositionChanged;
event Action<PicoInfo>                                     InfoReceived;
event Action                                               PongReceived;
event Action<string>                                       ErrorReceived;
event Action<string>                                       UnknownLineReceived;
```

**Outbound send methods:**
```csharp
SendPing / SendInfo / SendStatus
SendHome / SendJog(axis, dist, feed) / SendJogCancel / SendZero(axes)
SendFileList / SendFileLoad(name) / SendFileUnload() / SendFileDelete(name)
SendJobStart / SendJobPause / SendJobResume / SendJobAbort
SendSpindleOn(rpm) / SendSpindleOff
SendEstop / SendReset
SendOverrideFeed(%) / SendOverrideSpindle(%) / SendOverrideRapid(%)
```

---

## Phase 2 — `MainWindowViewModel` Rework

### Remove

- `ParseStatusReport()` — GRBL `<Idle|MPos:...>` string parsing
- `ApplyPinState()` — GRBL pin string parsing
- `SetAllAxesHomed()`, `ClearLimitStates()` — optimistic local mutation
- `ExecuteStart/Pause/Stop/Home/EStop()` — locally-driven state changes
- 200 ms `?` polling timer (Pico pushes state; no polling needed)
- `MotionState` and `SafetyState` as locally-driven properties

### Add

Subscribe to `PicoProtocolService` events and expose the results as read-only properties:

```csharp
// Set only by PicoProtocolService.StateChanged
public MachineOperationState MachineState { get; private set; }

// Set only by PicoProtocolService.CapsChanged
public CapsFlags Caps { get; private set; }

// Set only by PicoProtocolService.SafetyChanged
public SafetyLevel SafetyLevel { get; private set; }

// Set only by PicoProtocolService.PositionChanged
public double MachineX/Y/Z { get; private set; }
public double WorkX/Y/Z    { get; private set; }
```

`@EVENT` handler routes incoming events:

| Event name | Action |
|---|---|
| `JOB_PROGRESS` | Set `CurrentLine` from `LINE=`, notify `Progress` |
| `JOB_COMPLETE` | Notify UI |
| `JOB_ERROR` | Set `StatusMessage` with `REASON=` value |
| `SD_MOUNTED` / `SD_REMOVED` | Update SD state |
| `TEENSY_CONNECTED` / `TEENSY_DISCONNECTED` | Update Teensy link display |
| `ESTOP_ACTIVE` / `ESTOP_CLEARED` | Update safety display |
| `LIMIT` | Update limit axis display from `AXIS=` value |

All computed display properties (`MotionStateLabel`, `CanStart`, etc.) derive from
`MachineState` and `Caps` — no local rules.

### Keep

- `ActiveGCodeDocument` — set by `FilesViewModel` for visualization
- `TotalLines`, `CurrentFileName` — for display
- `PiConnectionStatus` — desktop→Pico link status
- `StatusMessage` / `IsStatusError` — status bar display

---

## Phase 3 — `ConnectViewModel` Rework

### Remove

- `?`-polling loop (15 sends over 3 s)
- `[PICO:]` / `[VER:]` / `[SN:]` message parsing

### New handshake flow

```
1. User selects port → OpenPort()
2. Send @PING → wait for PongReceived (timeout 3 s)
3. Send @INFO → wait for InfoReceived (timeout 2 s)
4. Parse PicoInfo: populate Firmware, Board, TeensyConnected
5. Success → PiConnectionStatus = Connected
   Failure → Disconnect, PiConnectionStatus = Error
```

`PicoProtocolService.InfoReceived` replaces manual `[PICO:]` / `[VER:]` / `[SN:]` parsing.

---

## Phase 4 — `ManualControlViewModel` Rework

### Remove

All direct `MainVm` mutations:

| Old (optimistic) | New (send command, await @STATE) |
|---|---|
| `MainVm.SetAllAxesHomed()` | `_protocol.SendHome()` |
| `MainVm.WorkX = 0` | `_protocol.SendZero("ALL")` |
| `MainVm.SpindleOn = true` | `_protocol.SendSpindleOn(rpm)` |
| `MainVm.WorkOffsetX = ...` | `_protocol.SendZero("X")` |

### Control availability

Bind directly to `MainVm.Caps.*` — no local `CanJog`, `CanHome`, etc. re-derivation:

```
Jog controls     → MainVm.Caps.Motion
Spindle controls → MainVm.Caps.Spindle
Override sliders → MainVm.Caps.Overrides
Probe button     → MainVm.Caps.Probe
```

---

## Phase 5 — `FilesViewModel` Rework

### File listing

Currently reads local filesystem. New flow:
- On connect: call `_protocol.SendFileList()`
- Handle `UnknownLineReceived` for `@FILE name SIZE=n` lines (or add to `PicoProtocolService`)
- On `@OK FILE_LIST_END`: populate `Files` collection

### File selection

- On select: keep selection local to the desktop preview state
- On `Load for Job`: call `_protocol.SendFileLoad(name)` (Pico loads it from SD)
- Loaded job snapshots come back from `@JOB NAME=<filename|NONE>`
- Still parse file locally via `GCodeParser` for the 3D preview (read-only)
- `ActiveGCodeDocument` still set locally for visualization

### Job progress

- Remove direct `CurrentLine = 0` / `Progress = 0` writes
- `CurrentLine` comes from `@EVENT JOB_PROGRESS LINE=n TOTAL=n`
- `Progress` computed from `CurrentLine / TotalLines`

---

## Phase 6 — Cleanup

- Delete `ParseStatusReport()`, `ApplyPinState()`, all remaining GRBL string handling
- Remove `[Obsolete]` stub enums (`MotionState`, `SafetyState`) from `Enums.cs`
- Remove dead `CanStart`/`CanPause`/`CanStop`/`CanHome`/`CanJog` local logic
- `DiagnosticsViewModel` — wire `UnknownLineReceived` for raw line display

---

## Build Order

```
✅  Phase 1 — Enums.cs new types + PicoProtocolService  (DONE)
✅  Phase 2 — MainWindowViewModel rework                 (DONE)
✅  Phase 3 — ConnectViewModel rework                   (DONE)
✅  Phase 4 — ManualControlViewModel rework             (DONE)
✅  Phase 5 — FilesViewModel rework                   (DONE)
    Phase 6 — Cleanup (remove [Obsolete] stubs, dead GRBL code)
```

Each phase keeps the project buildable. Phases 3–5 can be done in any order
once Phase 2 is complete (they all depend on `MainVm.Caps` and `PicoProtocolService`).
