# Portable CNC System Summary

This is the compact map of the system state machines and wire protocols. The longer reference docs are still useful for transition-table detail:

- `STATE_MACHINE.md`
- `STORAGE_TRANSFER_FSM.md`
- `PLAN_BINARY_PROTOCOL.md`

## Runtime Ownership

The system has three cooperating runtimes:

| Runtime | Owns | Does not own |
|---|---|---|
| Desktop app | Operator UI, file preview, serial connection UX, binary CMD/RESP/EVENT client | Machine truth, SD file truth, motion execution |
| Pico 2W | Desktop USB protocol, SD storage, transfer sessions, touchscreen UI, safety/caps projection, Pico-to-Teensy supervision | Physical motion generation |
| Teensy 4.1 | grblHAL motion state, G-code parsing, planner, stepper/spindle IO, true GRBL state | Desktop UX, SD transfer state |

Core rule: **Teensy/grblHAL is the source of truth for motion state.** The Pico maps Teensy state into its own machine FSM, then publishes binary state/caps/safety/position to the desktop.

## Protocol Stack

```text
Desktop
  |
  | USB CDC, COBS + CRC framed binary protocol
  | frame types 1..7
  v
Pico 2W
  |
  | UART ASCII command bridge, 115200 baud
  | @JOG / @HOME / @GRBL_STATE / @POS / ok / error
  v
Teensy 4.1 + grblHAL
```

## Desktop <-> Pico Protocol

Transport is COBS/CRC framed over USB CDC. Frame header:

```text
[type:u8][transfer_id:u8][flags:u8][seq:u32][payload_len:u16][payload][crc:u32]
```

| Frame | Direction | Meaning |
|---|---|---|
| `1 FRAME_UPLOAD_DATA` | Desktop -> Pico | Upload file data chunk |
| `2 FRAME_UPLOAD_ACK` | Pico -> Desktop | Upload chunk ACK |
| `3 FRAME_DOWNLOAD_DATA` | Pico -> Desktop | Download file data chunk |
| `4 FRAME_DOWNLOAD_ACK` | Desktop -> Pico | Download chunk ACK |
| `5 FRAME_CMD` | Desktop -> Pico | Typed command request |
| `6 FRAME_RESP` | Pico -> Desktop | Response to command |
| `7 FRAME_EVENT` | Pico -> Desktop | Unsolicited state/event push |

`FRAME_CMD`, `FRAME_RESP`, and `FRAME_EVENT` payloads begin with `message_type:u8`, then a packed struct from:

- Pico: `pico2W/src/protocol/protocol_defs.h`
- Desktop: `desktop/Protocol/ProtocolDefs.cs`

Main commands:

| Category | Commands |
|---|---|
| Connection/status | `PING`, `INFO`, `STATUS` |
| Motion/manual | `HOME`, `JOG`, `JOG_CANCEL`, `ZERO`, `PROBE_Z` |
| Job control | `START`, `PAUSE`, `RESUME`, `ABORT`, `ESTOP`, `RESET` |
| Spindle/override | `SPINDLE_ON`, `SPINDLE_OFF`, `OVERRIDE` |
| Storage | `FILE_LIST`, `FILE_LOAD`, `FILE_UNLOAD`, `FILE_DELETE`, `FILE_UPLOAD`, `FILE_UPLOAD_END`, `FILE_UPLOAD_ABORT`, `FILE_DOWNLOAD`, `FILE_DOWNLOAD_ACK`, `FILE_DOWNLOAD_ABORT` |
| Settings | `SETTINGS_GET`, `SETTINGS_SET` |

Main response/event outputs:

| Output | Purpose |
|---|---|
| `STATE` | Current Pico machine state |
| `CAPS` | Current allowed actions |
| `SAFETY` | Current safety level |
| `JOB` | Loaded job metadata |
| `POS` | Machine/work coordinates |
| `COMMAND_ACK` | Accepted command |
| `ERROR` / `STORAGE_ERROR` / `WAIT` | Rejected command, storage failure, or busy state |
| `TEENSY_CONNECTED` / `TEENSY_DISCONNECTED` | Motion link status |
| `JOB_PROGRESS` / `JOB_COMPLETE` / `JOB_ERROR` | Job-stream lifecycle |
| `SD_MOUNTED` / `SD_REMOVED` | Storage mount events |

## Pico <-> Teensy Protocol

Transport is newline-terminated ASCII over Pico `uart0` on `GP0/GP1` to Teensy `Serial1` on `D0/D1`.

```text
Pico GP0 TX -> Teensy D0 RX1
Pico GP1 RX <- Teensy D1 TX1
Baud: 115200
```

Pico-to-Teensy commands:

| Command | Meaning |
|---|---|
| `@PING` | Link probe |
| `@HOME` | Execute `$H` |
| `@JOG AXIS=X DIST=1.000 FEED=500` | Execute `$J=G91 G21 X1.000 F500` |
| `@JOG_CANCEL` | Enqueue realtime jog cancel `0x85` |
| `@GCODE <line>` | Queue streamed G-code line |
| `@RT_FEED_HOLD` | Enqueue feed hold |
| `@RT_CYCLE_START` | Enqueue cycle start |
| `@RT_RESET` | Enqueue soft reset |
| `@RT_ESTOP` | Enqueue reset-style E-stop response |
| `@UNLOCK` | Execute `$X` |
| `@ZERO AXIS=ALL/X/Y/Z` | Execute `G10 L20 P1 ...0` |
| `@SPINDLE_ON RPM=12000` | Execute `M3 S12000` |
| `@SPINDLE_OFF` | Execute `M5` |

Teensy-to-Pico lines:

| Line | Meaning |
|---|---|
| `@BOOT TEENSY_READY` | Teensy bridge booted |
| `@PONG` | Reply to `@PING` |
| `@GRBL_STATE IDLE/JOG/CYCLE/HOLD/...` | True grblHAL state |
| `@POS MX=... MY=... MZ=... WX=... WY=... WZ=...` | Machine/work position |
| `ok` | Accepted command or queued G-code |
| `error:<code>` | Command rejected or grblHAL error |

Current diagnostics:

| Macro | Effect |
|---|---|
| `PICO_UART_DEBUG_USB=0` | Full UART mirror off, avoids `@POS` spam |
| `PICO_UART_MANUAL_DEBUG_USB=1` | Logs manual control lines, translated GRBL command, and `ok/error` on Teensy USB serial |

## Machine FSM

Owned by Pico: `MachineFsm`.

Inputs:

- Teensy link events: `TeensyConnected`, `TeensyDisconnected`
- grblHAL state reports: `GrblIdle`, `GrblJog`, `GrblCycle`, `GrblHold*`, `GrblAlarm`, `GrblEstop`
- operator commands: `HomeCmd`, `JogCmd`, `StartCmd`, `PauseCmd`, `AbortCmd`, `ResetCmd`
- physical/storage/job events: `HwEstopAsserted`, `HwEstopCleared`, `SdMounted`, `SdRemoved`, `JobLoaded`, `JobUnloaded`

States:

| State | Meaning |
|---|---|
| `Booting` | Pico is up, waiting for Teensy boot/link |
| `Syncing` | Teensy link exists, waiting for true GRBL state |
| `TeensyDisconnected` | Motion UART is down and no active job is being protected |
| `Idle` | Ready for manual motion, file operations, or job start |
| `Homing` | grblHAL homing is active |
| `Jog` | grblHAL jog is active |
| `Starting` | Job stream started, waiting for motion |
| `Running` | Job/motion cycle active |
| `Hold` | Feed hold, safety door, or tool-change pause |
| `Fault` | grblHAL alarm |
| `Estop` | E-stop latched |
| `CommsFault` | Teensy link lost during active job |
| `Uploading` | Legacy enum; storage transfer FSM owns active uploads now |

GRBL-to-Pico state mapping:

| grblHAL | Pico |
|---|---|
| `STATE_IDLE` | `Idle`, except during active job where stream context decides |
| `STATE_HOMING` | `Homing` |
| `STATE_JOG` | `Jog` |
| `STATE_CYCLE` | `Running` |
| `STATE_HOLD`, `STATE_SAFETY_DOOR`, `STATE_TOOL_CHANGE` | `Hold` |
| `STATE_ALARM` | `Fault` |
| `STATE_ESTOP` | `Estop` |
| `STATE_SLEEP`, `STATE_CHECK_MODE` | Operationally treated as idle-ish for UI/caps |

Caps and safety are derived from machine state:

| Derived output | Meaning |
|---|---|
| `CAP_MOTION` | Manual motion/home-like controls allowed |
| `CAP_FILE_LOAD` | SD file operations allowed |
| `CAP_JOB_START` | Loaded job can start |
| `CAP_JOB_PAUSE/RESUME/ABORT` | Job controls allowed |
| `CAP_SPINDLE`, `CAP_OVERRIDES`, `CAP_RESET`, `CAP_PROBE` | Auxiliary permissions |
| `SAFETY_SAFE/MONITORING/WARNING/CRITICAL` | Orthogonal safety level for UI |

## Teensy/grblHAL State

Owned by grblHAL. Pico and desktop should treat it as the physical motion truth.

| grblHAL state | Meaning |
|---|---|
| `STATE_IDLE` | Ready/no motion |
| `STATE_ALARM` | Alarm, G-code locked out |
| `STATE_CHECK_MODE` | Dry-run mode |
| `STATE_HOMING` | Homing active |
| `STATE_CYCLE` | G-code/motion cycle active |
| `STATE_HOLD` | Feed hold active |
| `STATE_JOG` | Jog active |
| `STATE_SAFETY_DOOR` | Door pause |
| `STATE_SLEEP` | Low-power/disabled |
| `STATE_ESTOP` | E-stop state |
| `STATE_TOOL_CHANGE` | Tool-change pause |

The Teensy bridge reports state through `@GRBL_STATE ...`. It should report the initial state at boot/link and every later actual `state_get()` change.

## Job Stream FSM

Owned by Pico: `JobStreamStateMachine`.

Purpose: stream a loaded SD G-code file from Pico to Teensy over UART one line/batch at a time.

States:

| State | Meaning |
|---|---|
| `Idle` | No stream active |
| `Preparing` | Core worker is preparing/read-counting the file |
| `Beginning` | Job has been accepted and first stream batch is pending |
| `Streaming` | Lines are being sent, waiting for `ok` per line |
| `PausedByHold` | Stream paused while machine is held |
| `Cancelling` | Abort/cancel cleanup active |
| `Complete` | All lines sent and completed |
| `Faulted` | Stream read/send/error path failed |

Main flow:

```text
Idle -> Preparing -> Beginning -> Streaming -> Complete -> Idle
                         |             |
                         v             v
                      Faulted      PausedByHold
                         |             |
                         v             v
                      Idle/cleanup  Streaming
```

Important interaction:

- `ok` from Teensy advances the stream line window.
- `error:<code>` faults the stream.
- `STATE_HOLD` pauses stream progress.
- `STATE_IDLE` during a job is interpreted using stream flags: complete, abort, or between segments.

## Storage Transfer FSM

Owned by Pico: `StorageTransferStateMachine`.

Purpose: make SD card file operations deterministic and separate from machine motion state.

States:

| State | Meaning |
|---|---|
| `Idle` | No active file operation |
| `Listing` | Enumerating files |
| `Loading` | Loading/unloading active job metadata |
| `Deleting` | Deleting a file |
| `UploadOpen` | Preparing target file |
| `Uploading` | Receiving upload chunks |
| `UploadFinalizing` | Closing/verifying upload |
| `DownloadOpen` | Preparing source file |
| `Downloading` | Sending download chunks |
| `Aborting` | Cleanup requested |
| `Faulted` | Unrecovered storage/protocol fault |

Gate for new SD work:

```text
StorageState == Mounted
MachineOperationState in { Idle, TeensyDisconnected }
StorageTransferState == Idle
```

Main upload flow:

```text
Idle -> UploadOpen -> Uploading -> UploadFinalizing -> Idle
              \             \                 \
               -> Aborting   -> Aborting       -> Faulted/Idle on error
```

Main download flow:

```text
Idle -> DownloadOpen -> Downloading -> Idle
              \              \
               -> Aborting    -> Faulted/Idle on error
```

Transfer errors include busy, not allowed, no SD, file not found, invalid filename/session, bad sequence, size/CRC mismatch, read/write failure, no space, and aborted.

## Storage Mount State

Owned by Pico storage service.

| State | Meaning |
|---|---|
| `Uninitialized` | Storage not started |
| `Mounting` | SD mount in progress |
| `Mounted` | SD available |
| `MountError` | Mount failed |
| `ScanError` | File scan/listing failed |

This is separate from the transfer FSM. A mounted card can still have no active transfer.

## Loaded Job State

Owned by Pico job/loaded-job storage.

| State | Meaning |
|---|---|
| `NoJobLoaded` | No selected file |
| `JobLoaded` | A file is selected for run |
| `Running` | Compatibility/application-level job state; actual streaming uses `JobStreamStateMachine` |

Loaded job metadata feeds:

- `CAP_JOB_START`
- desktop file page state
- job stream preparation
- persisted restore on SD mount

## Core User Flows

### Connect

```text
Desktop selects USB port
Desktop sends CMD_PING / CMD_INFO / CMD_STATUS
Pico supervises Teensy UART with @PING
Teensy replies @PONG and @GRBL_STATE
Pico emits TEENSY_CONNECTED + STATE/CAPS/SAFETY/POS
Desktop shows connected + real machine state
```

### Manual jog

```text
Desktop CMD_JOG(axis, dist, feed)
Pico sends @JOG AXIS=... DIST=... FEED=...
Teensy translates to $J=G91 G21 ...
grblHAL enters STATE_JOG
Teensy emits @GRBL_STATE JOG
Pico maps to MachineOperationState::Jog
Desktop receives EVENT_STATE / RESP_STATE and shows JOG
grblHAL returns STATE_IDLE when complete
Teensy emits @GRBL_STATE IDLE
Pico maps to Idle
Desktop shows IDLE
```

### File upload

```text
Desktop CMD_FILE_UPLOAD(name, size, flags)
Pico transfer FSM: Idle -> UploadOpen -> Uploading
Desktop sends FRAME_UPLOAD_DATA chunks
Pico replies FRAME_UPLOAD_ACK per committed chunk
Desktop sends CMD_FILE_UPLOAD_END(expected CRC)
Pico transfer FSM: UploadFinalizing -> Idle
Pico emits upload complete + file-list changed
```

### Run loaded job

```text
Desktop CMD_START
Pico validates job loaded + machine/caps
JobStream: Idle -> Preparing -> Beginning -> Streaming
Pico sends @GCODE <line> to Teensy
Teensy replies ok/error
Pico advances or faults stream
Teensy @GRBL_STATE CYCLE/HOLD/IDLE updates machine FSM
JobStream Complete + GrblIdle -> Pico Idle + JOB_COMPLETE
```

### Pause/resume/abort

```text
Pause:  Desktop CMD_PAUSE  -> Pico @RT_FEED_HOLD    -> Teensy STATE_HOLD
Resume: Desktop CMD_RESUME -> Pico @RT_CYCLE_START  -> Teensy STATE_CYCLE/RUNNING
Abort:  Desktop CMD_ABORT  -> Pico cancels stream + @RT_RESET -> Teensy STATE_IDLE
```

## Debug Checklist

| Symptom | Check |
|---|---|
| Desktop says motion online but stuck `SYNCING` | Teensy must emit `@GRBL_STATE ...` after `@PONG` or state poll |
| Desktop enters `JOG` but stays there | Teensy must emit `@GRBL_STATE IDLE` after jog completes |
| Jog updates coordinates but motors do not move | grblHAL accepted motion; check step/dir/enable pins, driver enable polarity, motor power |
| Teensy serial is flooded with `@POS` | Keep `PICO_UART_DEBUG_USB=0`; use manual diagnostics only |
| Need manual command trace | Set `PICO_UART_MANUAL_DEBUG_USB=1` and watch Teensy USB serial |
| File operation rejected | Check SD mounted, machine state `Idle`/`TeensyDisconnected`, transfer FSM `Idle` |

## Source Files

| Concern | Main files |
|---|---|
| Desktop protocol client | `desktop/Services/PicoProtocolService.cs`, `desktop/Services/SerialService.cs` |
| Desktop state display | `desktop/ViewModels/MainWindowViewModel.cs` |
| Pico protocol server | `pico2W/src/protocol/desktop_protocol.cpp`, `pico2W/src/protocol/usb_cdc_transport.cpp` |
| Shared protocol definitions | `pico2W/src/protocol/protocol_defs.h`, `desktop/Protocol/ProtocolDefs.cs` |
| Pico machine FSM | `pico2W/src/app/machine/machine_fsm.cpp`, `pico2W/src/core/state_types.h` |
| Pico UART bridge client | `pico2W/src/app/comm/pico_uart_client.cpp` |
| Pico storage transfer FSM | `pico2W/src/app/storage/storage_transfer_fsm.cpp/.h` |
| Pico job stream FSM | `pico2W/src/app/stream/job_stream_state_machine.cpp/.h` |
| Teensy bridge | `teensy4.1/src/pico_framework.cpp` |
| grblHAL state definitions | `teensy4.1/src/grbl/system.h`, `teensy4.1/src/grbl/state_machine.c` |
