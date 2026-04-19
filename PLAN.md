# State Machine — Remaining Work Plan

## Context

The unified MachineFsm (13 states) on the Pico is fully implemented and the desktop is a correct stateless viewer. The gap is that the **Teensy still speaks a legacy protocol** (`@HELLO`, `@EVT MACHINE STATE=...`, `@CMD SEQ=n OP=...`) instead of the documented `@BOOT TEENSY_READY` / `@GRBL_STATE` / `@HOME` / `@GCODE` protocol. This means the Pico command handlers have **simulation stubs** that fake GRBL responses instead of forwarding to real hardware. Additionally, the E-stop GPIO and some desktop cleanup items are pending.

## Dependency Graph

```
Phase 1  (Teensy rewrite)      ──┐
Phase 1a (E-stop GPIO)  ───────┐ ├── Phase 2 (Pico stub removal + forwarding)
Phase 4  (Desktop cleanup) ──┐ │ │       │
                              │ │ │       └── Phase 3 (G-code streaming)
                              │ │ │
                         (all independent, can run in parallel)
```

---

## Phase 1: Teensy Firmware Redesign

**Status in PROTOCOL.md:** ❌ Redesign
**Files:** `teensy4.1/src/pico_framework.cpp` (rewrite), delete `my_stream.cpp` + `my_stream.h`
**Blocks:** Phase 2, Phase 3

### 1.1 Hook `grbl.on_state_change` → emit `@GRBL_STATE`

Register callback in `my_plugin_init()`. Map grblHAL `sys_state_t` bits:

| grblHAL state | Emit |
|---|---|
| `STATE_IDLE` (0) | `@GRBL_STATE IDLE` |
| `STATE_ALARM` | `@GRBL_STATE ALARM` |
| `STATE_HOMING` | `@GRBL_STATE HOMING` |
| `STATE_CYCLE` | `@GRBL_STATE CYCLE` |
| `STATE_HOLD` | `@GRBL_STATE HOLD SUBSTATE=<0\|1>` (check `sys.holding_state`) |
| `STATE_JOG` | `@GRBL_STATE JOG` |
| `STATE_ESTOP` | `@GRBL_STATE ESTOP` |
| `STATE_SAFETY_DOOR` | `@GRBL_STATE DOOR` |
| `STATE_TOOL_CHANGE` | `@GRBL_STATE TOOL_CHANGE` |
| `STATE_SLEEP` | `@GRBL_STATE SLEEP` |

### 1.2 Hook `grbl.on_realtime_report` → emit `@POS` at ~5 Hz

Throttle with 200ms timestamp guard. Extract machine pos from `sys.position` via `system_convert_array_steps_to_mpos()`, work pos via `gc_state.coord_system` + `gc_state.coord_offset`.

Format: `@POS MX=%.3f MY=%.3f MZ=%.3f WX=%.3f WY=%.3f WZ=%.3f`

### 1.3 Startup: `@BOOT TEENSY_READY`

Replace `@HELLO PROTO=1 CAPS=STATUS,JOG,MACHINING` with `@BOOT TEENSY_READY`.

### 1.4 New command dispatch (replace `@CMD SEQ=n OP=...`)

| Inbound | Action |
|---|---|
| `@PING` | Reply `@PONG` |
| `@HOME` | `system_execute_line("$H")` |
| `@JOG AXIS=X DIST=10.0 FEED=500` | Build `$J=G91 G21 X10.0 F500`, `protocol_enqueue_gcode()` |
| `@JOG_CANCEL` | `protocol_enqueue_realtime_command(0x85)` |
| `@GCODE <line>` | `protocol_enqueue_gcode(line)` → respond `ok` or `error:<code>` |
| `@RT_FEED_HOLD` | `protocol_enqueue_realtime_command(CMD_FEED_HOLD)` |
| `@RT_CYCLE_START` | `protocol_enqueue_realtime_command(CMD_CYCLE_START)` |
| `@RT_RESET` | `protocol_enqueue_realtime_command(CMD_RESET)` |
| `@RT_ESTOP` | `protocol_enqueue_realtime_command(CMD_RESET)` |
| `@UNLOCK` | `system_execute_line("$X")` |
| `@SPINDLE_ON RPM=<n>` | `protocol_enqueue_gcode("M3 S<n>")` |
| `@SPINDLE_OFF` | `protocol_enqueue_gcode("M5")` |
| `@ZERO AXIS=<X\|Y\|Z\|ALL>` | `protocol_enqueue_gcode("G10 L20 P1 X0 ...")` |

### 1.5 `@SENSORS` on pin change (optional, lower priority)

Emit `@SENSORS LIMIT_X=<0|1> LIMIT_Y=<0|1> LIMIT_Z=<0|1> PROBE=<0|1>` via `on_control_signals_changed` or polled.

### 1.6 Delete legacy code

- Delete `my_stream.cpp`, `my_stream.h`
- Remove `PicoMachineState`/`PicoJobState` enums, `report_state_changes()` polling
- Remove `@EVT` emission, `@HELLO`, `@CMD SEQ=n OP=...` parsing
- Remove `upload_job_file` / `stream_redirect_read` bulk-upload flow

### Verification

Connect serial terminal to Teensy UART. Confirm: `@BOOT TEENSY_READY` on power-up, `@GRBL_STATE IDLE` after boot, `@HOME` triggers `@GRBL_STATE HOMING` then `IDLE`, `@POS` at ~5 Hz during motion.

---

## Phase 1a: E-stop GPIO Wiring

**Status in PROTOCOL.md:** ❌ Follow-up
**Files:** `pico2W/src/app/portable_cnc_app.cpp`, `pico2W/src/config.h` (PIN_ESTOP=15 already defined)
**Independent** — can run in parallel with Phase 1

### Steps

1. **Init GPIO** in `run_startup_sequence()`: `gpio_init(PIN_ESTOP)`, `gpio_set_dir(GPIO_IN)`, `gpio_pull_up()` (active-low NC switch)
2. **Poll in main loop** (20ms / UI_POLL_MS): read `gpio_get(PIN_ESTOP)`, track previous state
3. **Debounce**: require 2-3 consecutive stable reads (40-60ms)
4. **On falling edge** (active): `machine_fsm_.handle_event(HwEstopAsserted)`, `desktop_protocol_.emit_state_update()`, `desktop_protocol_.emit_event("ESTOP_ACTIVE")`
5. **On rising edge** (cleared): `machine_fsm_.handle_event(HwEstopCleared)`, `desktop_protocol_.emit_state_update()`, `desktop_protocol_.emit_event("ESTOP_CLEARED")`

### Verification

Wire a momentary button to GP15+GND. Press → desktop shows ESTOP state. Release → caps show RESET=1. Send @RESET → returns to IDLE.

---

## Phase 2: Pico Stub Removal + Command Forwarding

**Blocked on:** Phase 1
**Files:** `desktop_protocol.cpp`, `desktop_protocol.h`, `pico_uart_client.cpp`, `pico_uart_client.h`, `portable_cnc_app.cpp`

### 2.1 Add forwarding methods to PicoUartClient

`send_home()`, `send_jog(axis, dist, feed)`, `send_jog_cancel()`, `send_gcode(line)`, `send_feed_hold()`, `send_cycle_start()`, `send_reset()`, `send_estop()`, `send_unlock()`, `send_spindle_on(rpm)`, `send_spindle_off()`, `send_zero(axis)`

### 2.2 Wire `uart_client_` into DesktopProtocol

Add `PicoUartClient&` as constructor parameter. Update `PortableCncApp` to pass it.

### 2.3 Replace 9 stubs with UART forwarding

For each handler: **keep** the FSM injection (`inject(MachineEvent::HomeCmd)` etc.), **remove** the fake completion (`inject(MachineEvent::GrblIdle)` etc.), **add** UART forward call.

| Handler | Keep | Remove (stub) | Add |
|---|---|---|---|
| `handle_home()` | `inject(HomeCmd)` | `inject(GrblIdle)` × 2 | `uart_.send_home()` |
| `handle_jog()` | `inject(JogCmd)` | `inject(GrblIdle)` × 2 | `uart_.send_jog(...)` |
| `handle_jog_cancel()` | `inject(JogStop)` | `inject(GrblIdle)` × 2 | `uart_.send_jog_cancel()` |
| `handle_start()` | `inject(StartCmd)` | `inject(GrblCycle,JobStreamComplete,GrblIdle)` | start streaming (Phase 3) |
| `handle_pause()` | — (no inject) | `inject(GrblHoldPending,GrblHoldComplete)` | `uart_.send_feed_hold()` |
| `handle_resume()` | — (no inject) | `inject(GrblCycle)` | `uart_.send_cycle_start()` |
| `handle_abort()` | `inject(AbortCmd)` | `inject(GrblIdle)` | `uart_.send_reset()` |
| `handle_estop()` | `inject(HwEstopAsserted)` | — | `uart_.send_estop()` |
| `handle_reset()` | `inject(ResetCmd)` | `inject(GrblIdle)` | `uart_.send_unlock()` or `send_reset()` |

Also wire `handle_spindle_on/off()` and `handle_zero()` (currently just emit @OK with no action).

### 2.4 Handle `ok`/`error:` from Teensy in PicoUartClient

Add parsing for bare `ok` and `error:<code>` responses before the `line[0] != '@'` filter. Needed for G-code streaming.

### 2.5 Update touch UI command paths

Update existing `pico_uart_client` methods (`hold()`, `resume()`, `jog()`, `home_all()`, `zero_all()`) from `@CMD SEQ=n OP=...` format to new send methods.

### Verification

Desktop: press Home → Pico forwards @HOME to Teensy → Teensy responds @GRBL_STATE HOMING → Pico FSM transitions → desktop sees HOMING state → Teensy finishes → @GRBL_STATE IDLE → desktop sees IDLE.

---

## Phase 3: G-code Streaming Convergence

**Blocked on:** Phase 2
**Files:** `pico_uart_client.cpp`, `pico_uart_client.h`, `desktop_protocol.cpp`

### 3.1 Add streaming state to PicoUartClient

```cpp
struct StreamingState {
    bool active = false;
    FIL file{};
    uint32_t lines_sent = 0;
    uint32_t total_lines = 0;
    bool waiting_for_ok = false;
};
```

### 3.2 Implement line-by-line streaming

- `start_streaming(entry)`: open SD file, send first `@GCODE` line, set `waiting_for_ok = true`
- In `poll()`: when `active && !waiting_for_ok`, read next line, send `@GCODE <line>`
- `handle_gcode_ok()`: set `waiting_for_ok = false`, increment counter, emit progress. On EOF → set `job_stream_complete_` on FSM
- `handle_gcode_error()`: set `abort_pending_` on FSM, send `@RT_RESET`, emit `@EVENT JOB_ERROR`

### 3.3 Wire `handle_start()` in DesktopProtocol

Replace stub with `uart_client_.start_streaming(*jobs_.loaded_entry())`.

### 3.4 Job progress events

Emit `@EVENT JOB_PROGRESS LINE=<n> TOTAL=<n>` every ~10 lines or 500ms.

### Verification

Load a file via desktop, press Start → lines stream one at a time → progress updates in desktop → job completes → IDLE state.

---

## Phase 4: Desktop Cleanup

**Independent** — can run in parallel with everything
**Files:** `Enums.cs`, `MainWindowViewModel.cs`, `SerialService.cs`, `DiagnosticsViewModel.cs`

### 4.1 Delete obsolete enums (`Enums.cs` lines 63-73)

Remove `[Obsolete] MotionState` and `[Obsolete] SafetyState`. Fix compile errors from remaining references.

### 4.2 Delete backward-compat stubs (`MainWindowViewModel.cs`)

Remove: `StartPolling()`, `StopPolling()`, `SetAllAxesHomed()`, `SetAxisHomed()`, `ClearLimitStates()`, the `MotionState` property adapter, the `SafetyState` property adapter.

### 4.3 Delete `SendRealtime()` (`SerialService.cs` line 85)

Unused — no call sites in any ViewModel.

### 4.4 Wire `DiagnosticsViewModel.SendCommand()` (line 94)

Replace `AddLog("RX", "ok")` placeholder with actual `Protocol.SendRawLine()` call + response listener.

### 4.5 `DiagnosticsViewModel.RefreshSensors()` (line 100)

Replace hardcoded temps (42.5, 31.0, 35.0) with real telemetry. **Blocked on**: `@SENSORS` from Teensy (Phase 1.5) and actual thermistor hardware.

### Verification

`dotnet build desktop/desktop.csproj -o desktop/buildverify` passes. No `[Obsolete]` warnings from deleted stubs.

---

## Execution Order

| Order | Phase | Can start | Blocked on |
|---|---|---|---|
| 1 | Phase 1 (Teensy rewrite) | Now | — |
| 1 | Phase 1a (E-stop GPIO) | Now | — |
| 1 | Phase 4 (Desktop cleanup) | Now | — |
| 2 | Phase 2 (Pico forwarding) | After Phase 1 | Phase 1 |
| 3 | Phase 3 (G-code streaming) | After Phase 2 | Phase 2 |

## Key Risks

1. **HOLD substate**: `sys.holding_state` must be checked in `on_state_change` to distinguish `SUBSTATE=0` (pending) from `SUBSTATE=1` (complete). The Pico FSM needs this for `job_resume` cap.
2. **`@GCODE` response routing**: Teensy must capture GRBL's `ok`/`error` for each `@GCODE` line and send it back on UART1. Use `protocol_enqueue_gcode()` which returns `status_code_t`.
3. **Legacy parser removal timing**: Pico's `handle_event_line()` handles both `@EVT` (legacy) and `@GRBL_STATE` (new). Keep legacy handlers until Teensy is fully migrated, then remove.
4. **E-stop dual path**: GPIO on Pico (Phase 1a) and software `@ESTOP` from desktop (Phase 2) are independent paths that both must work.
