# Pico 2W <-> Teensy 4.1 UART Protocol

This protocol is based on the current Pico UI state machines in:

- [pico2W/src/core/state_types.h](/C:/Users/farzi/Documents/Git/portable-cnc-machine/pico2W/src/core/state_types.h)
- [pico2W/src/app/machine/machine_state_machine.cpp](/C:/Users/farzi/Documents/Git/portable-cnc-machine/pico2W/src/app/machine/machine_state_machine.cpp)
- [pico2W/src/app/job/job_state_machine.cpp](/C:/Users/farzi/Documents/Git/portable-cnc-machine/pico2W/src/app/job/job_state_machine.cpp)
- [pico2W/src/app/jog/jog_state_machine.h](/C:/Users/farzi/Documents/Git/portable-cnc-machine/pico2W/src/app/jog/jog_state_machine.h)

It replaces the ad-hoc Teensy-side `Loading / Ready / Paused / Complete / Error` protocol state exposure in [teensy4.1/src/pico_framework.cpp](/C:/Users/farzi/Documents/Git/portable-cnc-machine/teensy4.1/src/pico_framework.cpp) with the Pico's existing UI state model.

![UART scheme](/C:/Users/farzi/Documents/Git/portable-cnc-machine/docs/uart/pico-teensy-uart-scheme.svg)

## 1. Physical Link

- Bus: UART, point-to-point, 3.3 V TTL
- Baud: `115200`
- Format: `8N1`
- Flow control: none
- Terminator: `LF` (`\n`)
- Wiring:
  - Pico `GP0` TX -> Teensy pin `1` RX1
  - Pico `GP1` RX <- Teensy pin `0` TX1
  - Pico GND <-> Teensy GND

This matches [WIRING.md](/C:/Users/farzi/Documents/Git/portable-cnc-machine/WIRING.md) and the current Teensy `Serial1` usage in [teensy4.1/src/pico_framework.cpp](/C:/Users/farzi/Documents/Git/portable-cnc-machine/teensy4.1/src/pico_framework.cpp).

## 2. Ownership Model

The cleanest split for this project is:

- Pico owns UI-local state:
  - `MachineState::Booting`
  - `MachineState::Calibrating`
  - `JobState::NoFileSelected`
  - `JobState::FileSelected`
  - `StorageState::*`
  - selected file metadata shown on-screen
- Teensy owns machine execution state:
  - `MachineState::Idle`
  - `MachineState::Running`
  - `MachineState::Hold`
  - `MachineState::Alarm`
  - `MachineState::Estop`
- Pico owns jog intent and job file source.
- Teensy owns motion execution, grblHAL command enqueueing, alarms, and machine position.

That means the protocol should expose Pico-compatible states, but only the subset each MCU can really authoritatively control.

## 3. State Mapping

### Machine state shown by the Pico UI

| Pico UI state | UART authority | Teensy source |
|---|---|---|
| `Booting` | Pico | before UART hello completes |
| `Calibrating` | Pico | local touch calibration only |
| `Idle` | Teensy | `state_get() == STATE_IDLE` |
| `Running` | Teensy | `STATE_CYCLE` or active job stream |
| `Hold` | Teensy | `STATE_HOLD` or `STATE_TOOL_CHANGE` |
| `Alarm` | Teensy | `STATE_ALARM` |
| `Estop` | Teensy or Pico | `STATE_ESTOP`, or Pico-triggered estop until Teensy confirms |

### Job state shown by the Pico UI

| Pico job state | UART meaning |
|---|---|
| `NoFileSelected` | no file chosen on Pico, or selection cleared after abort |
| `FileSelected` | file chosen on Pico and accepted by Teensy for the next run |
| `Running` | run accepted and g-code stream is active |

### Important change for the Teensy implementation

The current Teensy plugin exposes transport-oriented states:

- `Idle`
- `Loading`
- `Ready`
- `Running`
- `Paused`
- `Complete`
- `Error`

For this project, those should become internal implementation details only. Over UART, the Teensy should report Pico-compatible states instead:

- `Loading` and `Ready` map to machine `Idle`
- `Paused` maps to machine `Hold`
- `Complete` maps to machine `Idle` with job `FileSelected`
- `Error` maps to machine `Alarm` unless the error is a true estop condition

## 4. Frame Format

Every control frame is one ASCII line:

```text
@<TYPE> [KEY=VALUE] [KEY=VALUE] ...\n
```

Rules:

- Lines beginning with `@` are protocol frames.
- Lines not beginning with `@` are raw g-code lines during an active job stream.
- Keys are uppercase ASCII.
- Values contain no spaces. Use `_` for multiword symbolic values.
- The Pico should include `SEQ=<n>` on every command that expects a reply.
- The Teensy echoes that same `SEQ` in `@ACK` or `@ERR`.

Examples:

```text
@HELLO ROLE=PICO PROTO=1
@HELLO ROLE=TEENSY PROTO=1 FW=GRBLHAL
@CMD SEQ=12 OP=RUN
@ACK SEQ=12
@EVT MACHINE STATE=RUNNING GRBL=STATE_CYCLE
```

## 5. Message Set

### 5.1 Startup and link health

Pico -> Teensy:

```text
@HELLO ROLE=PICO PROTO=1
@PING SEQ=1
@REQ SEQ=2 WHAT=STATUS
```

Teensy -> Pico:

```text
@HELLO ROLE=TEENSY PROTO=1 FW=GRBLHAL CAPS=STATUS,JOG,JOB_STREAM
@ACK SEQ=1 PONG=1
@EVT MACHINE STATE=IDLE GRBL=STATE_IDLE
@EVT JOB STATE=NO_FILE_SELECTED
@EVT POS X=0.000 Y=0.000 Z=0.000
```

### 5.2 Job selection and job lifecycle

Pico -> Teensy:

```text
@CMD SEQ=10 OP=JOB_SELECT INDEX=2
@CMD SEQ=11 OP=JOB_BEGIN INDEX=2 LINES=524
G90
G21
G0 X0 Y0
...
@CMD SEQ=12 OP=JOB_END
@CMD SEQ=13 OP=RUN
```

Teensy -> Pico:

```text
@ACK SEQ=10
@EVT JOB STATE=FILE_SELECTED INDEX=2
@ACK SEQ=11
@ACK SEQ=12
@ACK SEQ=13
@EVT JOB STATE=RUNNING INDEX=2
@EVT MACHINE STATE=RUNNING GRBL=STATE_CYCLE
@EVT JOB_PROGRESS SENT=128 TOTAL=524
```

Recommended behavior:

- `JOB_SELECT` only synchronizes UI intent.
- `JOB_BEGIN` opens a stream on the Teensy.
- Raw g-code lines follow immediately.
- `JOB_END` closes the stream and validates that the run can start.
- `RUN` is only accepted if:
  - machine state is `Idle` or `Hold`
  - a valid selected file exists
  - the job stream has ended cleanly

### 5.3 Hold, resume, abort, estop

Pico -> Teensy:

```text
@CMD SEQ=20 OP=HOLD
@CMD SEQ=21 OP=RESUME
@CMD SEQ=22 OP=ABORT
@CMD SEQ=23 OP=ESTOP
@CMD SEQ=24 OP=RESET_TO_IDLE
```

Teensy -> Pico:

```text
@ACK SEQ=20
@EVT MACHINE STATE=HOLD GRBL=STATE_HOLD
@ACK SEQ=21
@EVT MACHINE STATE=RUNNING GRBL=STATE_CYCLE
@ACK SEQ=22
@EVT MACHINE STATE=IDLE GRBL=STATE_IDLE
@EVT JOB STATE=FILE_SELECTED INDEX=2
@ACK SEQ=23
@EVT MACHINE STATE=ESTOP GRBL=STATE_ESTOP
@ACK SEQ=24
@EVT MACHINE STATE=IDLE GRBL=STATE_IDLE
```

Recommended semantics:

- `ABORT` is a soft stop and reset of the current streamed job.
- `ESTOP` is reserved for the physical emergency-stop path and must force the Teensy into `Estop`.
- `RESET_TO_IDLE` is only valid from `Hold` or `Alarm`, matching the Pico state machine.

### 5.4 Jog commands

Pico `JogAction` values map cleanly to UART commands:

| Pico action | UART command |
|---|---|
| `MoveXNegative` | `@CMD SEQ=n OP=JOG AXIS=X DIST_MM=-step FEED=feed` |
| `MoveXPositive` | `@CMD SEQ=n OP=JOG AXIS=X DIST_MM=+step FEED=feed` |
| `MoveYNegative` | `@CMD SEQ=n OP=JOG AXIS=Y DIST_MM=-step FEED=feed` |
| `MoveYPositive` | `@CMD SEQ=n OP=JOG AXIS=Y DIST_MM=+step FEED=feed` |
| `MoveZNegative` | `@CMD SEQ=n OP=JOG AXIS=Z DIST_MM=-step FEED=feed` |
| `MoveZPositive` | `@CMD SEQ=n OP=JOG AXIS=Z DIST_MM=+step FEED=feed` |
| `HomeAll` | `@CMD SEQ=n OP=HOME_ALL` |
| `ZeroAll` | `@CMD SEQ=n OP=ZERO_ALL` |

Notes:

- `step` comes directly from `JogStateMachine::step_size_mm()`.
- `feed` comes directly from `JogStateMachine::feed_rate_mm_min()`.
- `SelectStep*` and `SelectFeed*` stay Pico-local and do not need UART messages until the user requests motion.

### 5.5 Status events from Teensy

The Teensy should proactively publish these:

```text
@EVT MACHINE STATE=IDLE|RUNNING|HOLD|ALARM|ESTOP GRBL=...
@EVT JOB STATE=NO_FILE_SELECTED|FILE_SELECTED|RUNNING INDEX=n
@EVT JOB_PROGRESS SENT=n TOTAL=m
@EVT POS X=... Y=... Z=...
@EVT LIMITS X=0 Y=0 Z=0
@EVT ALARM CODE=n
@EVT MSG TEXT=...
```

Suggested rates:

- `MACHINE`, `JOB`, `ALARM`: on change only
- `POS`: 5 Hz while jogging or running, 1 Hz while idle
- `JOB_PROGRESS`: every 8-16 lines, not every line

## 6. Why this protocol fits the current repo

It matches the current code structure:

- The Pico already models machine, job, and jog separately.
- The Teensy already uses `Serial1` and line-oriented parsing.
- The Pico build already enables UART stdio in [pico2W/CMakeLists.txt](/C:/Users/farzi/Documents/Git/portable-cnc-machine/pico2W/CMakeLists.txt).
- The existing Teensy plugin already distinguishes control lines from g-code lines by prefix, which is worth keeping.

It also fixes two project issues:

- The current Teensy job buffer is capped by `PICO_JOB_BUFFER_LINES`, which is too small for real files.
- The current Teensy state names do not match the Pico UI state machine and would force awkward translation logic in the UI.

## 7. Recommended Teensy refactor

Update [teensy4.1/src/pico_framework.cpp](/C:/Users/farzi/Documents/Git/portable-cnc-machine/teensy4.1/src/pico_framework.cpp) like this:

1. Keep a transport/parser layer on `Serial1`.
2. Replace exposed UART states with:
   - machine state mirror: `IDLE`, `RUNNING`, `HOLD`, `ALARM`, `ESTOP`
   - job state mirror: `NO_FILE_SELECTED`, `FILE_SELECTED`, `RUNNING`
3. Treat `Loading`, `Ready`, and `Complete` as internal stream substates only.
4. Accept Pico-style commands: `JOB_SELECT`, `JOB_BEGIN`, `JOB_END`, `RUN`, `HOLD`, `RESUME`, `ABORT`, `ESTOP`, `RESET_TO_IDLE`, `JOG`, `HOME_ALL`, `ZERO_ALL`.
5. Emit `@EVT ...` lines whenever grblHAL state changes.
6. Stream g-code incrementally instead of storing the whole file as a fixed 64-line job.

## 8. Minimal example exchange

```text
PICO   -> @HELLO ROLE=PICO PROTO=1
TEENSY -> @HELLO ROLE=TEENSY PROTO=1 FW=GRBLHAL CAPS=STATUS,JOG,JOB_STREAM
TEENSY -> @EVT MACHINE STATE=IDLE GRBL=STATE_IDLE
TEENSY -> @EVT JOB STATE=NO_FILE_SELECTED

PICO   -> @CMD SEQ=30 OP=JOB_SELECT INDEX=0
TEENSY -> @ACK SEQ=30
TEENSY -> @EVT JOB STATE=FILE_SELECTED INDEX=0

PICO   -> @CMD SEQ=31 OP=JOB_BEGIN INDEX=0 LINES=3
PICO   -> G21
PICO   -> G90
PICO   -> G0 X0 Y0
PICO   -> @CMD SEQ=32 OP=JOB_END
PICO   -> @CMD SEQ=33 OP=RUN
TEENSY -> @ACK SEQ=31
TEENSY -> @ACK SEQ=32
TEENSY -> @ACK SEQ=33
TEENSY -> @EVT JOB STATE=RUNNING INDEX=0
TEENSY -> @EVT MACHINE STATE=RUNNING GRBL=STATE_CYCLE
TEENSY -> @EVT JOB_PROGRESS SENT=3 TOTAL=3
TEENSY -> @EVT MACHINE STATE=IDLE GRBL=STATE_IDLE
TEENSY -> @EVT JOB STATE=FILE_SELECTED INDEX=0
```

## 9. Summary

Use the Pico's existing state model as the public protocol.

- Pico-local: calibration, storage, file metadata, jog step/feed selection
- Teensy-local: motion execution, alarms, estop, machine position
- Shared over UART: machine state, job state, jog commands, streamed g-code, and event updates

That gives the project a protocol that matches the UI already in the repo instead of forcing the Pico UI to adapt to a temporary Teensy-only state machine.
