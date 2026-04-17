# Portable CNC Machine — Communication Protocol

## Overview

The Pico 2W is the **sole state owner** for the machine. All clients — the desktop app
(USB CDC-ACM) and the web UI (WiFi AP, later) — are stateless viewers that send commands
and receive state updates. The Teensy 4.1 handles motion execution only and is not visible
to clients directly.

```
Desktop App  ──── USB CDC-ACM ────┐
                                  ▼
Web UI (later) ── WiFi AP ──── [ PICO 2W ]  ── UART1 ──  Teensy 4.1 (grblHAL)
                                  │
                              SD Card
                              E-stop / Safety
                              Touch UI
```

---

## Transport

| Link              | Transport     | Baud / Detail         |
|-------------------|---------------|-----------------------|
| Desktop → Pico    | USB CDC-ACM   | 115200, line-delimited |
| Web UI → Pico     | WiFi AP / HTTP | TBD (later)           |
| Pico → Teensy     | UART1 GP20/21 | 115200, line-delimited |

All messages are **newline-terminated ASCII lines**. No binary framing.

---

## Convention

All protocol lines follow the same style across all links:

```
@VERB [PARAM[=VALUE] ...]
```

- Lines starting with `@` are protocol messages
- Commands are sent by a client (Desktop → Pico, or Pico → Teensy)
- Responses and events are sent by the responder (Pico → Desktop, or Teensy → Pico)
- All keywords are uppercase
- Parameters are space-separated key=value pairs where needed
- Blank lines are ignored

### Response convention

| Prefix       | Meaning                              |
|--------------|--------------------------------------|
| `@OK ...`    | Command accepted / succeeded         |
| `@ERROR ...` | Command rejected or failed           |
| `@STATE ...` | Unsolicited or polled state update   |
| `@CAPS ...`  | Capability flags — sent alongside every `@STATE` change |
| `@EVENT ...` | Unsolicited event notification       |
| `@WAIT ...`  | Busy, retry later                    |

### Capability flags (`@CAPS`)

The Pico is the sole decision-maker on what actions are currently valid. Rather than
the desktop re-deriving this from state strings (which would duplicate logic across
two codebases), the Pico explicitly sends a `@CAPS` line alongside every `@STATE`
change.

```
@CAPS MOTION=<0|1> PROBE=<0|1> SPINDLE=<0|1> FILE_LOAD=<0|1> JOB_START=<0|1> JOB_PAUSE=<0|1> JOB_RESUME=<0|1> JOB_ABORT=<0|1> OVERRIDES=<0|1> RESET=<0|1>
```

| Flag          | Value | Condition (computed by Pico)                                              |
|---------------|-------|---------------------------------------------------------------------------|
| `MOTION`      | 0/1   | state == IDLE                                                             |
| `PROBE`       | 0/1   | state == IDLE && all_axes_homed                                           |
| `SPINDLE`     | 0/1   | state == IDLE \|\| RUNNING \|\| HOLD                                      |
| `FILE_LOAD`   | 0/1   | state == IDLE && sd_mounted                                               |
| `JOB_START`   | 0/1   | state == IDLE && job_loaded && teensy_connected && all_axes_homed         |
| `JOB_PAUSE`   | 0/1   | state == RUNNING                                                          |
| `JOB_RESUME`  | 0/1   | state == HOLD && hold_complete                                            |
| `JOB_ABORT`   | 0/1   | state == RUNNING \|\| HOLD \|\| STARTING                                  |
| `OVERRIDES`   | 0/1   | state == RUNNING \|\| HOLD                                                |
| `RESET`       | 0/1   | state == FAULT \|\| (state == ESTOP && !hw_estop_active)                  |

Each flag gates exactly one UI control or group. The desktop binds these bools
directly — no interpretation, no string comparisons, no duplicated rules.

The Pico computes `@CAPS` from its full internal context: machine state, Teensy link
state, job loaded state, SD state, homing state. The desktop never needs to know the rules.

**Example — IDLE, file loaded, all axes homed, SD mounted:**
```
@STATE IDLE
@CAPS MOTION=1 PROBE=1 SPINDLE=1 FILE_LOAD=1 JOB_START=1 JOB_PAUSE=0 JOB_RESUME=0 JOB_ABORT=0 OVERRIDES=0 RESET=0
```

**Example — job running:**
```
@STATE RUNNING
@CAPS MOTION=0 PROBE=0 SPINDLE=1 FILE_LOAD=0 JOB_START=0 JOB_PAUSE=1 JOB_RESUME=0 JOB_ABORT=1 OVERRIDES=1 RESET=0
```

**Example — job in hold (deceleration complete):**
```
@STATE HOLD
@CAPS MOTION=0 PROBE=0 SPINDLE=1 FILE_LOAD=0 JOB_START=0 JOB_PAUSE=0 JOB_RESUME=1 JOB_ABORT=1 OVERRIDES=1 RESET=0
```

**Example — Teensy disconnected:**
```
@STATE TEENSY_DISCONNECTED
@CAPS MOTION=0 PROBE=0 SPINDLE=0 FILE_LOAD=0 JOB_START=0 JOB_PAUSE=0 JOB_RESUME=0 JOB_ABORT=0 OVERRIDES=0 RESET=0
```

**Example — FAULT (GRBL alarm), E-stop not active:**
```
@STATE FAULT
@CAPS MOTION=0 PROBE=0 SPINDLE=0 FILE_LOAD=0 JOB_START=0 JOB_PAUSE=0 JOB_RESUME=0 JOB_ABORT=0 OVERRIDES=0 RESET=1
```

**Example — SD card file upload in progress:**
```
@STATE UPLOADING
@CAPS MOTION=0 PROBE=0 SPINDLE=0 FILE_LOAD=0 JOB_START=0 JOB_PAUSE=0 JOB_RESUME=0 JOB_ABORT=0 OVERRIDES=0 RESET=0
```

`UPLOADING` is entered from `IDLE` when `@FILE_UPLOAD` is received. It exits to `IDLE`
on `@FILE_UPLOAD_END`, `@FILE_UPLOAD_ABORT`, or USB disconnect cleanup. All action caps
are suppressed. The Pico touch screen displays filename and upload progress percentage.
An E-stop received during `UPLOADING` aborts the upload, deletes the partial file, and
transitions to `ESTOP`.

---

## Pico State

The Pico owns the following state at all times, collapsed into a single unified
`MachineStateMachine` (see `STATE_MACHINE.md` for the full design):

**Operation state** (13 mutually exclusive):
`BOOTING` → `TEENSY_DISCONNECTED` | `SYNCING` → `IDLE` | `HOMING` | `STARTING` |
`RUNNING` | `HOLD` | `FAULT` | `ESTOP` | `COMMS_FAULT` | `JOG` | `UPLOADING`

**Safety level** (orthogonal): `SAFE` | `MONITORING` | `WARNING` | `CRITICAL`

**Internal flags** (used to compute `@CAPS` and drive transitions):
`teensy_connected`, `job_session_active`, `job_loaded`, `all_axes_homed`,
`sd_mounted`, `hw_estop_active`, `hold_complete`, `job_stream_complete`, `abort_pending`

---

## Commands: Desktop → Pico

### Connection / Handshake

```
@PING
```
Response: `@OK PONG`

Used by the desktop to confirm the Pico is alive on the port.

```
@INFO
```
Response: `@INFO FIRMWARE=<ver> BOARD=PICO2W TEENSY=<CONNECTED|DISCONNECTED>`

Returns Pico firmware version and whether the Teensy UART link is up.

---

### Status

```
@STATUS
```
Response: `@STATE MACHINE=<state> JOB=<state> SD=<state> TEENSY=<CONNECTED|DISCONNECTED>`

Poll the full Pico state snapshot.

---

### Job Control

```
@BEGIN_JOB
```
Response: `@OK BEGIN_JOB`
Puts Pico into job-loading mode. Subsequent lines that do not start with `@` are treated as G-code.

```
@END_JOB
```
Response: `@OK END_JOB LINES=<n>`
Closes the job buffer and marks it ready.

```
@CLEAR_JOB
```
Response: `@OK CLEAR_JOB`

```
@START
```
Response: `@OK START`
Begins executing the loaded job.

```
@PAUSE
```
Response: `@OK PAUSE`

```
@RESUME
```
Response: `@OK RESUME`

```
@ABORT
```
Response: `@OK ABORT`

---

### File Management (SD Card)

```
@FILE_LIST
```
Response (one line per file, terminated by `@OK FILE_LIST_END`):
```
@FILE <name> SIZE=<bytes>
@FILE <name> SIZE=<bytes>
...
@OK FILE_LIST_END COUNT=<n> FREE=<bytes>
```

`FREE` is the number of bytes remaining free on the SD card. Used by the desktop to
gate uploads and display storage info.

```
@FILE_LOAD NAME=<filename>
```
Response: `@OK FILE_LOAD NAME=<filename>`
Loads a file from SD as the active job. Valid only in IDLE state (`FILE_LOAD` cap must be 1).

```
@FILE_UNLOAD
```
Response: `@OK FILE_UNLOAD`
Unloads the active job file. Valid only in IDLE state.

Desktop and TFT preview selection are local UI state and are never sent over the protocol.
The protocol only represents the active loaded job on the Pico.
The Pico persists the loaded job by filename in flash and attempts to restore it after boot
or SD remount if the same file still exists on the card.

```
@FILE_DELETE NAME=<filename>
```
Response: `@OK FILE_DELETE NAME=<filename>` or `@ERROR FILE_NOT_FOUND`

---

### File Upload (Desktop → SD Card)

Transfers a `.gcode` file from the desktop to the Pico SD card over USB CDC.
Upload is only accepted when the Pico is in the `IDLE` state.

#### 1. Initiate

```
@FILE_UPLOAD NAME=<filename> SIZE=<bytes> [OVERWRITE=1]
```

| Response | Meaning |
|---|---|
| `@OK FILE_UPLOAD_READY NAME=<filename>` | Pico ready to receive chunks |
| `@ERROR FILE_EXISTS NAME=<filename>` | File exists; resend with `OVERWRITE=1` to replace |
| `@ERROR SD_FULL FREE=<bytes>` | Not enough space on SD card |
| `@ERROR SD_NOT_MOUNTED` | No SD card present |
| `@ERROR UPLOAD_BUSY` | Another upload already in progress |
| `@ERROR INVALID_STATE` | Pico not in IDLE state |

If `OVERWRITE=1` is sent and the file exists, the Pico deletes it before writing.
The desktop must warn the user and require confirmation before setting this flag.

#### 2. Send Chunks

Each chunk carries up to 192 raw bytes encoded as base64 (256 base64 characters max).
The desktop sends chunks sequentially and waits for acknowledgement before sending the next.

```
@CHUNK SEQ=<n> DATA=<base64>
```

Response:
```
@OK CHUNK SEQ=<n>
```

- `SEQ` starts at 0 and increments by 1 per chunk
- The Pico writes each decoded chunk to the SD file immediately upon receiving it

**Chunk error response:**
```
@ERROR CHUNK SEQ=<n> REASON=<reason>
```

| `REASON` | Meaning | Desktop action |
|---|---|---|
| `BAD_BASE64` | Data field could not be decoded | Resend chunk (up to 3 retries) |
| `BAD_SEQ` | SEQ number out of order | Resend from the expected SEQ (up to 3 retries) |
| `SD_WRITE_FAIL` | SD card write failed | Abort immediately — SD card likely full or removed |
| `UNKNOWN` | Unspecified error | Abort immediately |

On 3 consecutive failed retries for the same chunk, the desktop sends `@FILE_UPLOAD_ABORT`
and surfaces an error to the user.

#### 3. Finalise

After the last chunk, the desktop sends:

```
@FILE_UPLOAD_END CRC=<crc32hex>
```

`CRC` is the CRC-32 of the complete raw file content, expressed as 8 lowercase hex digits
(e.g. `a3f2c1b0`). The Pico verifies the CRC against what was written to SD.

| Response | Meaning |
|---|---|
| `@OK FILE_UPLOAD_END NAME=<filename> SIZE=<bytes>` | File written and verified |
| `@ERROR FILE_UPLOAD_CRC_FAIL` | CRC mismatch — Pico deletes the partial file |
| `@ERROR SD_WRITE_FAIL` | Final flush to SD failed — Pico deletes the partial file |

#### 4. Abort

The desktop may abort at any point (user cancel, timeout, or error) by sending:

```
@FILE_UPLOAD_ABORT
```

Response: `@OK FILE_UPLOAD_ABORT`

The Pico deletes the partial file from SD and resets upload state.

**Abort timeout:** If `@OK FILE_UPLOAD_ABORT` is not received within 3 seconds, the desktop
discards the upload state locally regardless — the Pico will eventually time out on its own
(see USB disconnect handling below). The desktop must not retry the abort.

#### 5. Timeouts

| Condition | Timeout | Desktop action |
|---|---|---|
| No `@OK FILE_UPLOAD_READY` after `@FILE_UPLOAD` | 3 s | Surface error, reset state — no abort needed (upload never started) |
| No `@OK CHUNK` or `@ERROR CHUNK` after sending a chunk | 5 s | Resend chunk (counts as a retry); after 3 timeouts → abort |
| No `@OK FILE_UPLOAD_END` after `@FILE_UPLOAD_END` | 5 s | Surface error, send `@FILE_UPLOAD_ABORT` |
| No `@OK FILE_UPLOAD_ABORT` after `@FILE_UPLOAD_ABORT` | 3 s | Discard locally, do not retry |

#### 6. USB Disconnect During Upload

**Pico behaviour:** The Pico must detect USB CDC disconnection (via the TinyUSB disconnect
callback or equivalent). On disconnect while an upload is in progress, the Pico must:
1. Delete the partial file from SD immediately
2. Reset upload state to idle
3. When the desktop reconnects and sends `@PING` / `@INFO`, the Pico is already clean

**Desktop behaviour:** `Serial.ErrorOccurred` fires and `OnDeviceLost()` is called on
`MainWindowViewModel`. The active upload in `FilesViewModel` must be cancelled immediately:
- No `@FILE_UPLOAD_ABORT` is sent (serial is gone)
- The upload `CancellationToken` is cancelled
- Upload state resets to idle
- UI shows "Upload failed: device disconnected"

`FilesViewModel` must subscribe to a disconnect notification — either directly via
`MainWindowViewModel.PiConnectionStatus` changing to `Error`, or via a dedicated
`UploadCancelled` callback from `MainWindowViewModel.OnDeviceLost()`.

#### 7. SD Card Removed During Upload

If `@EVENT SD_REMOVED` is received while an upload is in progress:

**Pico behaviour:** The Pico must abort the upload internally (the partial file is already
gone since the SD was removed), reset upload state, and emit:
```
@ERROR UPLOAD_SD_REMOVED
```

**Desktop behaviour:** On receiving `@ERROR UPLOAD_SD_REMOVED`, the desktop:
- Cancels the upload immediately (no abort command needed)
- Resets upload state
- Shows "Upload failed: SD card removed"

`@ERROR UPLOAD_SD_REMOVED` is a distinct error code so the desktop can show a specific
message rather than a generic failure.

#### Full exchange example

```
Desktop  →  @FILE_UPLOAD NAME=bracket_v2.gcode SIZE=3200 OVERWRITE=1
Pico     →  @OK FILE_UPLOAD_READY NAME=bracket_v2.gcode
Desktop  →  @CHUNK SEQ=0 DATA=<256 base64 chars>
Pico     →  @OK CHUNK SEQ=0
Desktop  →  @CHUNK SEQ=1 DATA=<256 base64 chars>
Pico     →  @OK CHUNK SEQ=1
...
Desktop  →  @CHUNK SEQ=16 DATA=<partial, last chunk>
Pico     →  @OK CHUNK SEQ=16
Desktop  →  @FILE_UPLOAD_END CRC=a3f2c1b0
Pico     →  @OK FILE_UPLOAD_END NAME=bracket_v2.gcode SIZE=3200
```

#### Throughput note

At 115200 baud (~11,520 bytes/sec raw), each chunk line is ~280 characters including
overhead. Accounting for base64 overhead (75% efficiency) and round-trip ACK latency,
expect roughly **6–8 KB/s** effective throughput. A 100 KB G-code file takes ~15 seconds.
A 1 MB file takes ~2 minutes. The desktop must show a progress bar during upload.

#### `@CAPS` during upload

The Pico must suppress all `@CAPS` flags that would allow conflicting operations
(motion, job start, file select) while an upload is in progress. It should emit:

```
@STATE IDLE
@CAPS MOTION=0 PROBE=0 SPINDLE=0 FILE_LOAD=0 JOB_START=0 ...
```

after receiving `@FILE_UPLOAD` and restore normal caps after `@FILE_UPLOAD_END` or
`@FILE_UPLOAD_ABORT`.

---

### Jog

```
@JOG AXIS=<X|Y|Z> DIST=<mm> FEED=<mm/min>
```
Response: `@OK JOG`

---

### Safety

```
@ESTOP
```
Response: `@OK ESTOP`
Immediately halts all motion. Latches until `@RESET`.

```
@RESET
```
Response: `@OK RESET`
Clears latched E-stop (only valid when safe to do so).

---

## Events: Pico → Desktop (unsolicited)

The Pico pushes these without being polled:

```
@EVENT STATE_CHANGED MACHINE=<state>
@EVENT JOB_PROGRESS LINE=<n> TOTAL=<n>
@EVENT JOB_COMPLETE
@EVENT JOB_ERROR REASON=<reason>
@EVENT SD_MOUNTED
@EVENT SD_REMOVED
@EVENT TEENSY_CONNECTED
@EVENT TEENSY_DISCONNECTED
@EVENT ESTOP_ACTIVE
@EVENT ESTOP_CLEARED
@EVENT LIMIT AXIS=<X|Y|Z>
```

---

## Pico → Teensy Protocol (redesigned — replaces pico_framework.cpp)

`pico_framework.cpp` will be redesigned from scratch. The new protocol replaces the
old buffered job model with direct GRBL state reporting and line-by-line G-code streaming
with standard `ok`/`error` flow control.

### Teensy → Pico (unsolicited push)

| Message                         | When sent                                                              |
|---------------------------------|------------------------------------------------------------------------|
| `@BOOT TEENSY_READY`            | On Teensy power-up / reset                                             |
| `@GRBL_STATE <state>`           | On every GRBL state change (IDLE, ALARM, HOMING, CYCLE, HOLD, JOG, …) |
| `@GRBL_STATE HOLD SUBSTATE=<0\|1>` | HOLD with substate: 0=Hold_Pending, 1=Hold_Complete                 |
| `@POS MX=<f> MY=<f> MZ=<f> WX=<f> WY=<f> WZ=<f>` | Periodic machine + work position (≈ 5 Hz)   |
| `@SENSORS LIMIT_X=<0\|1> LIMIT_Y=<0\|1> LIMIT_Z=<0\|1> PROBE=<0\|1>` | On pin change |
| `ok`                            | G-code line accepted by GRBL planner buffer                            |
| `error:<code>`                  | G-code line rejected by GRBL                                           |

> **Note:** `@GRBL_STATE` is the critical new requirement. The Teensy framework must hook
> into grblHAL's `on_state_change` callback and emit this line on every transition.
> The Pico cannot update its `MachineStateMachine` without it.

### Pico → Teensy (commands)

| Command                              | Effect on Teensy                                          |
|--------------------------------------|-----------------------------------------------------------|
| `@PING`                              | Responds `@OK PONG` — link health check                   |
| `@HOME`                              | Sends `$H` to GRBL                                        |
| `@JOG AXIS=<X\|Y\|Z> DIST=<f> FEED=<f>` | Sends `$J=G91 G21 <axis><dist> F<feed>` to GRBL       |
| `@JOG_CANCEL`                        | Sends jog cancel realtime byte (0x85) to GRBL             |
| `@GCODE <line>`                      | Sends a single G-code line — used for streaming and manual |
| `@RT_FEED_HOLD`                      | Sends realtime feed hold byte (0x21 `!`) to GRBL          |
| `@RT_CYCLE_START`                    | Sends realtime cycle start byte (0x7E `~`) to GRBL        |
| `@RT_RESET`                          | Sends realtime soft reset byte (0x18) to GRBL             |
| `@RT_ESTOP`                          | Sends realtime soft reset + sets Pico safety level CRITICAL |
| `@UNLOCK`                            | Sends `$X` to GRBL (alarm clear, FAULT recovery only)     |
| `@SPINDLE_ON RPM=<n>`                | Sends `M3 S<n>` to GRBL                                   |
| `@SPINDLE_OFF`                       | Sends `M5` to GRBL                                        |
| `@ZERO AXIS=<X\|Y\|Z\|ALL>`         | Sends `G10 L20 P1 <axis>0` to GRBL                        |

### G-code streaming (job execution)

The Pico streams a job from SD card to the Teensy using standard GRBL flow control:

1. Pico reads lines from the SD file and sends them one at a time via `@GCODE <line>`
2. Teensy forwards each line to GRBL and responds `ok` or `error:<code>`
3. Pico waits for `ok` before sending the next line
4. On `error:<code>`: Pico sets `abort_pending_`, sends `@RT_RESET`, emits `@EVENT JOB_ERROR`
5. On `@GRBL_STATE IDLE` with `job_session_active_` and `abort_pending_`: transition → IDLE
6. On `@GRBL_STATE IDLE` with `job_session_active_` and `job_stream_complete_`: emit `@EVENT JOB_COMPLETE`, transition → IDLE

No buffering on the Pico side beyond the current in-flight line. The Teensy's GRBL
planner buffer (15-line default) provides all necessary look-ahead.

---

## State Machines — Design Status

The unified state machine design is fully specified in `STATE_MACHINE.md`. All existing
Pico and Teensy state machine code is considered outdated and will be replaced.

### Pico (`pico2W/src/`)

| Component | Status | Action |
|---|---|---|
| `MachineStateMachine` | ❌ Redesign | Replace with 12-state unified FSM per `STATE_MACHINE.md` |
| `JobStateMachine` | ❌ Remove | Merged into `MachineStateMachine` (STARTING/RUNNING/HOLD states own job logic) |
| `JogStateMachine` | ⚠️ Partial keep | Remove position tracking. Keep UI prefs (step size, feed rate) |
| `StorageState` | ✅ Keep | Correct and complete — feeds events into `MachineStateMachine` |
| Teensy link tracking | ❌ Build new | `TEENSY_DISCONNECTED`/`SYNCING` states in `MachineStateMachine` + ping watchdog |
| Safety / E-stop | ❌ Build new | `ESTOP` state + safety level (SAFE/MONITORING/WARNING/CRITICAL) in `MachineStateMachine` |
| G-code streamer | ❌ Build new | Pico reads SD line-by-line, streams via `@GCODE`, tracks `ok`/`error` responses |
| E-stop GPIO | ❌ Wire up | `PIN_ESTOP = GP15` defined in `config.h` — not yet read anywhere |

### Teensy (`teensy4.1/src/`)

| Component | Status | Action |
|---|---|---|
| `pico_framework.cpp` | ❌ Redesign | Replace entirely. Must: hook `on_state_change` → emit `@GRBL_STATE`, implement new command set, pass G-code lines directly to GRBL, forward realtime bytes |
| `my_stream.cpp/.h` | ❌ Delete | Replaced by new streaming model in `pico_framework.cpp` |

### Desktop (`desktop/`)

| Component | Status | Action |
|---|---|---|
| `MainWindowViewModel` shadow state | ❌ Remove | Local `MotionState`/`SafetyState` mutation, optimistic homing flags, GRBL status parsing — all must go |
| `PicoProtocolService` | ❌ Build new | Replaces `SerialService` GRBL parser — pure `@`-protocol sink |
| `MotionState`/`SafetyState` enums | ⚠️ Keep as display enums | Must never be written to directly — only set by parsing Pico messages |

---

## Open Questions

- [x] **Desktop → Pico protocol vs Pico → Teensy protocol**: Desktop uses high-level `@START`
      etc.; Pico translates and breaks them down into GRBL realtime bytes / `@GCODE` lines.
      The two links intentionally have different command vocabularies.
- [x] **Teensy disconnection recovery**: `MachineStateMachine` enters `TEENSY_DISCONNECTED`.
      Pico retries the UART link with a ping watchdog. On `@BOOT TEENSY_READY` → `SYNCING`
      → reads first `@GRBL_STATE` → routes to appropriate state.
- [x] **Sensor forwarding**: Pico translates sensor state into `@EVENT` lines (limit hit,
      probe triggered) and position into `@POS` updates. No raw Teensy data forwarded.
- [x] **DRO update rate**: Teensy pushes `@POS` at ≈ 5 Hz from `on_realtime_report` callback.
      Pico forwards directly to desktop. No polling needed.
- [x] File upload protocol — chunked base64 over USB CDC. See **File Upload** section above.
- [ ] Web UI protocol — same `@`-lines over WebSocket, or REST/SSE over HTTP?

---

## Implementation Order (proposed)

All implementation details are governed by `STATE_MACHINE.md`. Build in this order
to minimize blocked work:

1. **Teensy `pico_framework.cpp` redesign** — everything else is blocked on `@GRBL_STATE`
   - Hook `on_state_change` → emit `@GRBL_STATE <state>` (with HOLD substate)
   - Hook `on_realtime_report` → emit `@POS MX=… MY=… MZ=… WX=… WY=… WZ=…` at 5 Hz
   - Implement new command set (see Pico → Teensy table above)
   - Pass `@GCODE <line>` content directly to GRBL input; respond `ok`/`error:<code>`
   - Delete `my_stream.cpp` / `my_stream.h`

2. **Pico UART1 driver** (GP20/GP21, 115200) — Pico ↔ Teensy line-framed transport

3. **Pico `MachineStateMachine` redesign** — 12 states, 10 caps, all flags per `STATE_MACHINE.md`
   - Wire `@GRBL_STATE` events from UART1 driver
   - Wire `StorageService` events (SdMounted / SdRemoved)
   - Wire E-stop GPIO (GP15) → HwEstopAsserted / HwEstopCleared
   - Wire ping watchdog for Teensy link health

4. **Pico G-code streaming engine** — reads SD file, sends lines via `@GCODE`, tracks `ok`/`error`

5. **Pico USB CDC-ACM driver** — Pico ↔ Desktop line-framed transport

6. **Pico protocol dispatcher** — routes incoming desktop `@`-commands to `MachineStateMachine`

7. **Desktop `PicoProtocolService`** — replaces GRBL serial parser with `@`-protocol sink

8. **Desktop `MainWindowViewModel` rework** — pure `@STATE`/`@CAPS`/`@EVENT` sink, remove shadow state

9. SD file management commands (`@FILE_LIST`, `@FILE_LOAD`, `@FILE_UNLOAD`, `@FILE_DELETE`)

10. WiFi AP + web UI (later)

---

## Desktop App — Stale Code to Remove / Rework

The desktop was originally written assuming it was a direct GRBL client with its own local
state machine. This is wrong under the new architecture. The Pico owns all state; the desktop
is a stateless viewer. The following must be addressed before implementing the new protocol.

### `MainWindowViewModel.cs`

**Problem: Acts as a state machine**

- `MotionState` and `SafetyState` are mutable properties driven by the desktop itself
- `ExecuteStart()`, `ExecutePause()`, `ExecuteStop()`, `ExecuteHome()`, `ExecuteEStop()`
  all mutate state **locally** without waiting for any confirmation from the Pico
- `SetAllAxesHomed()`, `ClearLimitStates()`, `ApplyPinState()` manage homing and limit
  state on the desktop
- `ParseStatusReport()` parses raw GRBL `<Idle|MPos:...>` lines and drives state from them —
  this entire parser is obsolete under the `@`-protocol
- `OnGrblLine()` parses GRBL-specific prefixes (`Grbl `, `[VER:`, `[PICO:`, `ok`, `ALARM:`)
  — all to be replaced with `@`-protocol message handling
- The polling loop sends raw `?` bytes every 200ms — obsolete, Pico will push `@EVENT` updates

**What it becomes:**
- Receives `@STATE` and `@EVENT` lines from `SerialService` and updates display-only properties
- Sends `@`-commands in response to user actions — no local state mutation
- No GRBL parsing, no polling loop, no state machine logic

---

### `ManualControlViewModel.cs`

**Problem: Makes state decisions and mutates state directly**

- `Jog()` builds raw GRBL strings (`$J=G91 G21 X... F...`) — should send `@JOG` to Pico
- `Home()` calls `MainVm.SetAllAxesHomed(true)` immediately — homed state must come from
  a Pico `@EVENT` response, not be set optimistically on the desktop
- `Zero()` and `ZeroAll()` directly write `MainVm.WorkX/Y/Z` and `WorkOffsetX/Y/Z` —
  work offset state must come from the Pico
- `StartSpindle()` sets `MainVm.SpindleOn = true` and `SpindleSpeed` immediately —
  spindle state must be confirmed by a Pico `@EVENT`
- `StopSpindle()`, `AlarmUnlock()`, `SoftReset()` all mutate state without a round trip
- All `SetCommandPreview()` calls are fine — UI-only, keep them
- All lockout reason strings (`GetJogLockoutReason()` etc.) are fine — display-only, keep them

**What it becomes:**
- Sends `@JOG`, `@HOME`, `@ZERO`, `@ESTOP` etc. to Pico and waits for `@EVENT`/`@STATE`
- Never writes to `MainVm` state properties directly
- All `Can*` guards remain but read from Pico-reported state only

---

### `Enums.cs`

**Problem: Desktop-owned state enums**

- `MotionState` — maps to Pico machine states. Keep as a display-side enum but values must
  be parsed from incoming `@STATE MACHINE=<value>` strings, not set by desktop logic
- `SafetyState` — same situation
- `ConnectionStatus` — fine to keep, this is genuinely desktop-side (port open/closed)

---

### `ConnectViewModel.cs`

**Problem: Handshake probes with raw `?` and waits for `[PICO:...]`**

- `ConnectAsync()` sends `?` (GRBL realtime byte) and waits for a `[PICO:...]` line —
  both the command and expected response are wrong
- After connecting, sends `$I` (GRBL info command) — obsolete
- Calls `MainVm.StartPolling()` which fires raw `?` every 200ms — to be removed

**What it becomes:**
- Opens port, sends `@PING`, waits for `@OK PONG` → Pico confirmed
- Sends `@INFO`, waits for `@INFO FIRMWARE=... TEENSY=<CONNECTED|DISCONNECTED>` → step 2
- If `TEENSY=CONNECTED`, mark fully online → step 3
- No polling loop — Pico pushes state changes via `@EVENT`

---

### `SerialService.cs`

**Currently fine structurally** — opens port, fires `LineReceived` per line, has `SendCommand`
and `SendRealtime`. The `SendRealtime` method (used for `?`, `!`, `~`, `0x18`) becomes
obsolete since the `@`-protocol has no realtime bytes. Can be removed after the rework.

---

### Summary table

| File | Action |
|------|--------|
| `MainWindowViewModel.cs` | Remove GRBL parser, polling loop, local state mutation. Become a pure `@`-event sink |
| `ManualControlViewModel.cs` | Replace raw GRBL command builders with `@`-commands. Remove direct state writes |
| `Enums.cs` | Keep enums, remove state machine semantics — values come from Pico strings |
| `ConnectViewModel.cs` | Replace `?` / `$I` handshake with `@PING` / `@INFO` flow |
| `SerialService.cs` | Remove `SendRealtime`, keep the rest |
| `DiagnosticsViewModel.cs` | Review — likely fine if it just logs received lines |
