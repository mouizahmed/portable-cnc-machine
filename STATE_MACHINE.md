# Portable CNC Machine — Unified State Machine

## Overview

A single `MachineStateMachine` on the Pico owns the full observable state of the machine
as seen by the desktop and touch UI. All other subsystems feed events into it. All outputs
(`@STATE`, `@CAPS`, `@SAFETY`) are derived from it.

### Architectural principle

**GRBL on the Teensy is the source of truth for motion state.** The Pico does not own or
replicate motion state — it receives GRBL state change notifications from the Teensy via
the @-protocol, maps them to its own unified state, and layers its own concerns
(connectivity, safety supervision, storage, job streaming) on top.

```
GRBL state change (Teensy)
        │
        │  @GRBL_STATE <state>   (Pico↔Teensy UART)
        ▼
  MachineStateMachine (Pico)
        │  maps GRBL state + Pico-layer context
        │
        ├─ @STATE <state>
        ├─ @CAPS MOTION=x SPINDLE=x ...
        ├─ @SAFETY <level>
        ▼
  Desktop / Touch / Web   (stateless viewers)
```

### Core rule

**The Pico never assumes a command it sends to the Teensy succeeded.** Operator actions
trigger commands to GRBL; GRBL's confirmed state change drives the Pico's state transition.
The Pico stays in its current state until a `@GRBL_STATE` event arrives.

---

## GRBL State Reference

grblHAL on the Teensy defines these mutually exclusive states (from `grbl/system.h`):

| GRBL State          | Value     | grblHAL behavior                                         |
|---------------------|-----------|----------------------------------------------------------|
| `STATE_IDLE`        | 0         | No motion, ready for commands                            |
| `STATE_ALARM`       | bit(0)    | Software alarm. Locks out G-code. Clear with `$X`        |
| `STATE_CHECK_MODE`  | bit(1)    | G-code dry-run, no motion                                |
| `STATE_HOMING`      | bit(2)    | Homing cycle active                                      |
| `STATE_CYCLE`       | bit(3)    | Motion executing (G-code streaming)                      |
| `STATE_HOLD`        | bit(4)    | Feed hold. Substates: Pending (decelerating) / Complete  |
| `STATE_JOG`         | bit(5)    | Jog move in progress. Cancel with `0x85`                 |
| `STATE_SAFETY_DOOR` | bit(6)    | Door ajar. Retracts, de-energizes, waits for door close  |
| `STATE_SLEEP`       | bit(7)    | Steppers de-energized, low-power                         |
| `STATE_ESTOP`       | bit(8)    | Hardware E-stop. Blocks everything including soft reset   |
| `STATE_TOOL_CHANGE` | bit(9)    | Manual tool change. Suspends input, waits for cycle start |

### Key grblHAL behaviors

- **Soft reset** (`0x18`): calls `mc_reset()` → kills spindle/coolant → resets planner/stepper
  → transitions to **`STATE_IDLE`** directly. Blocked while hardware E-stop pin is held.
- **`$X` unlock**: the standard alarm recovery. Clears `STATE_ALARM` → `STATE_IDLE`. Only
  works from `STATE_ALARM`. Does **not** work from `STATE_ESTOP`.
- **`$H` homing**: enters `STATE_HOMING` → on success `STATE_IDLE` → on failure `STATE_ALARM`.
- **Feed hold** (`!`): enters `STATE_HOLD` with `Hold_Pending` (decelerating) → once
  stopped, `Hold_Complete`. Resume with `~` (`CMD_CYCLE_START`).
- **Jog cancel** (`0x85`): decelerates current jog → `STATE_IDLE`.
- **Safety door**: enters `STATE_SAFETY_DOOR` → parking retract → de-energizes spindle/coolant
  → waits for door close + cycle start.
- **ESTOP exit**: hardware pin must be released first, then soft reset (`0x18`) → `STATE_IDLE`.

---

## Pico Operation States

| State                 | Description                                                                        |
|-----------------------|------------------------------------------------------------------------------------|
| `BOOTING`             | Pico powered up, waiting for Teensy `@BOOT TEENSY_READY`                          |
| `SYNCING`             | Teensy connected, waiting for first `@GRBL_STATE` to learn actual GRBL state       |
| `TEENSY_DISCONNECTED` | UART link to Teensy is down with no active job — reconnect to recover              |
| `IDLE`                | Teensy connected, GRBL confirmed idle, ready for commands                          |
| `HOMING`              | GRBL executing homing cycle                                                        |
| `JOG`                 | GRBL executing jog move                                                            |
| `STARTING`            | Job streaming begun, GRBL processing preamble — waiting for first motion           |
| `RUNNING`             | Job active, GRBL in cycle (or briefly idle between motion segments)                |
| `HOLD`                | Feed hold, safety door, or tool change — motion paused, resumable                  |
| `FAULT`               | GRBL alarm — `$X` unlock is the standard recovery                                  |
| `COMMS_FAULT`         | Teensy UART lost during an active job — reconnect to recover                       |
| `ESTOP`               | E-stop latched — hardware pin must be released, then soft reset to recover         |
| `UPLOADING`           | Desktop file transfer in progress — all action caps suppressed until complete       |

---

## GRBL State → Pico State Mapping

The mapping depends on whether a job session is active (`job_session_active_` flag).

### When no job session is active

| GRBL State          | Pico State  | Notes                                           |
|---------------------|-------------|--------------------------------------------------|
| `STATE_IDLE`        | `IDLE`      |                                                  |
| `STATE_HOMING`      | `HOMING`    |                                                  |
| `STATE_JOG`         | `JOG`       |                                                  |
| `STATE_CYCLE`       | `RUNNING`   | Unexpected but handled                           |
| `STATE_HOLD`        | `HOLD`      |                                                  |
| `STATE_SAFETY_DOOR` | `HOLD`      | Recoverable pause with door open                 |
| `STATE_TOOL_CHANGE` | `HOLD`      | Waiting for tool change acknowledgement          |
| `STATE_ALARM`       | `FAULT`     |                                                  |
| `STATE_ESTOP`       | `ESTOP`     |                                                  |
| `STATE_SLEEP`       | `IDLE`      | Steppers off but operationally idle              |
| `STATE_CHECK_MODE`  | `IDLE`      | Dry-run, treat as idle for UI                    |

### When a job session is active (`job_session_active_ == true`)

| GRBL State          | Pico State  | Notes                                                          |
|---------------------|-------------|----------------------------------------------------------------|
| `STATE_IDLE`        | *(context)* | See "GrblIdle during a job session" below                      |
| `STATE_CYCLE`       | `RUNNING`   |                                                                |
| `STATE_HOLD`        | `HOLD`      |                                                                |
| `STATE_SAFETY_DOOR` | `HOLD`      | Door opened during job — parking sequence active               |
| `STATE_TOOL_CHANGE` | `HOLD`      | M6 encountered during job                                      |
| `STATE_ALARM`       | `FAULT`     | Job session cancelled                                          |
| `STATE_ESTOP`       | `ESTOP`     | Job session cancelled                                          |

#### GrblIdle during a job session

GRBL can return to `STATE_IDLE` during an active job for several reasons. The Pico
disambiguates using internal flags:

| `job_session_active_` | `abort_pending_` | `job_stream_complete_` | Pico state | Action                             |
|-----------------------|------------------|------------------------|------------|------------------------------------|
| false                 | —                | —                      | → `IDLE`   | No session — GRBL finished or reset independently (e.g. reconnect via SYNCING) |
| true                  | true             | —                      | → `IDLE`   | Abort confirmed, clear job session |
| true                  | false            | true                   | → `IDLE`   | Job complete, emit `@EVENT JOB_COMPLETE`, clear session |
| true                  | false            | false                  | stay       | Between motion segments — stay in current state (`RUNNING` or `STARTING`) |

---

## Safety Level (orthogonal dimension)

The Pico's supervisor role. Runs independently of operation state, based on physical sensor
readings.

| Level        | Description                                                             |
|--------------|-------------------------------------------------------------------------|
| `SAFE`       | No active motion, all sensors within limits                             |
| `MONITORING` | Machine in motion (RUNNING/HOMING/JOG/STARTING), sensors polled at high rate |
| `WARNING`    | Sensor threshold exceeded — temperature, vibration, etc.                |
| `CRITICAL`   | E-stop active                                                           |

### Safety level transitions

| Current Level | Condition                                                  | Next Level   |
|---------------|------------------------------------------------------------|--------------|
| `SAFE`        | Operation state enters RUNNING, HOMING, JOG, or STARTING  | `MONITORING` |
| `MONITORING`  | Operation state exits to IDLE and sensors normal           | `SAFE`       |
| `MONITORING`  | Sensor threshold exceeded                                  | `WARNING`    |
| `WARNING`     | Sensors return within limits and motion active             | `MONITORING` |
| `WARNING`     | Sensors return within limits and no motion                 | `SAFE`       |
| `WARNING`     | Condition worsens beyond tolerance                         | Pico triggers abort → `SAFE` after stop |
| Any           | E-stop asserted                                            | `CRITICAL`   |
| `CRITICAL`    | E-stop cleared + reset confirmed                           | `SAFE`       |

When entering `FAULT` or `COMMS_FAULT` from a motion state: motion has stopped.
If sensors are normal, safety drops to `SAFE`. If a sensor condition persists, stays `WARNING`.

---

## Internal Flags (not states)

| Flag                    | Type | Set by                                | Cleared by                              |
|-------------------------|------|---------------------------------------|-----------------------------------------|
| `teensy_connected_`     | bool | `TeensyConnected` event               | `TeensyDisconnected` event              |
| `job_selected_`         | bool | `JobSelected` event                   | `JobDeselected`, `SdRemoved`            |
| `job_session_active_`   | bool | `StartCmd` accepted (enters STARTING) | Job complete, abort confirmed, fault    |
| `job_stream_complete_`  | bool | All lines sent + all `ok`s received   | Cleared at job start                    |
| `abort_pending_`        | bool | `AbortCmd` sent to Teensy             | `GrblIdle` received (abort confirmed)   |
| `hold_complete_`        | bool | `@GRBL_STATE HOLD SUBSTATE=COMPLETE`  | Any non-HOLD state entered              |
| `all_axes_homed_`       | bool | GRBL reports all axes homed           | GRBL alarm, soft reset, power cycle     |
| `sd_mounted_`           | bool | `SdMounted` event                     | `SdRemoved` event                       |
| `upload_active_`        | bool | `FileUploadCmd` accepted              | `UploadComplete`, `UploadAborted`, `UploadFailed` |
| `hw_estop_active_`      | bool | `PIN_ESTOP` GPIO asserted             | `PIN_ESTOP` GPIO deasserted             |

> `job_session_active_` is true from the moment `StartCmd` is accepted through to job
> completion or cancellation. It does not imply GRBL is in motion — during `STARTING`,
> GRBL may still be processing non-motion preamble. Use `state_ == RUNNING` to determine
> whether GRBL has entered `STATE_CYCLE`.

### Flag clear-on-entry checklist

Whenever the state machine enters one of these states, the listed flags must be explicitly
cleared. This is an implementation contract — not inferring it from transition rows is the
most common source of stale-flag bugs.

| Entering state         | Clear these flags                                                  |
|------------------------|--------------------------------------------------------------------|
| `STARTING`             | `job_stream_complete_`, `abort_pending_`                           |
| `RUNNING`              | *(none — flags carry over from STARTING)*                          |
| `HOLD`                 | `hold_complete_` (will be re-set when SUBSTATE=COMPLETE arrives)   |
| `IDLE`                 | `job_session_active_`, `abort_pending_`, `job_stream_complete_`, `hold_complete_` |
| `FAULT`                | `job_session_active_`, `abort_pending_`, `job_stream_complete_`, `hold_complete_`, `all_axes_homed_` |
| `COMMS_FAULT`          | `job_session_active_`, `abort_pending_`, `job_stream_complete_`, `hold_complete_`, `all_axes_homed_` |
| `ESTOP`                | `job_session_active_`, `abort_pending_`, `job_stream_complete_`, `hold_complete_`, `all_axes_homed_`, `upload_active_` |
| `UPLOADING`            | *(none — `upload_active_` is set by the event, not cleared on entry)*             |
| `TEENSY_DISCONNECTED`  | `job_session_active_`, `abort_pending_`, `job_stream_complete_`, `hold_complete_`, `teensy_connected_`, `all_axes_homed_` |
| `SYNCING`              | `hold_complete_` (re-evaluated on first `@GRBL_STATE`)             |

Additional rules:
- `all_axes_homed_` is cleared when entering `FAULT`, `ESTOP`, `COMMS_FAULT`, or `TEENSY_DISCONNECTED` (position may be lost)
- `hw_estop_active_` is managed by GPIO ISR, not by state entry
- `sd_mounted_` and `job_selected_` are managed by `StorageService`, not by state entry
- `upload_active_` is managed by the upload protocol handler. It is set when `FileUploadCmd` is accepted and cleared when the upload terminates for any reason (complete, abort, or E-stop cleanup)

---

## Events

### From Teensy (UART)

| Event                 | Wire format                           | Description                                           |
|-----------------------|---------------------------------------|-------------------------------------------------------|
| `TeensyConnected`     | `@BOOT TEENSY_READY`                  | Teensy finished boot, GRBL initialised                |
| `TeensyDisconnected`  | UART timeout / line error             | Lost contact with Teensy                              |
| `GrblIdle`            | `@GRBL_STATE IDLE`                    | GRBL entered `STATE_IDLE`                             |
| `GrblHoming`          | `@GRBL_STATE HOMING`                  | GRBL entered `STATE_HOMING`                           |
| `GrblCycle`           | `@GRBL_STATE CYCLE`                   | GRBL entered `STATE_CYCLE`                            |
| `GrblHold`            | `@GRBL_STATE HOLD SUBSTATE=<P\|C>`    | GRBL entered `STATE_HOLD` (Pending or Complete)       |
| `GrblJog`             | `@GRBL_STATE JOG`                     | GRBL entered `STATE_JOG`                              |
| `GrblAlarm`           | `@GRBL_STATE ALARM CODE=<n>`          | GRBL entered `STATE_ALARM` with alarm code            |
| `GrblEstop`           | `@GRBL_STATE ESTOP`                   | GRBL entered `STATE_ESTOP`                            |
| `GrblDoor`            | `@GRBL_STATE DOOR`                    | GRBL entered `STATE_SAFETY_DOOR`                      |
| `GrblToolChange`      | `@GRBL_STATE TOOL_CHANGE`             | GRBL entered `STATE_TOOL_CHANGE`                      |
| `GrblSleep`           | `@GRBL_STATE SLEEP`                   | GRBL entered `STATE_SLEEP`                            |
| `GcodeAck`            | `ok`                                  | GRBL acknowledged a G-code line                       |
| `GcodeError`          | `error:<n>`                           | GRBL rejected a G-code line — **abort on any error**  |

### From operator (desktop / touch UI)

| Event            | Description                                              |
|------------------|----------------------------------------------------------|
| `HomeCmd`        | User requests homing                                     |
| `JogCmd`         | User requests jog move (axis, distance, feed)            |
| `JogStop`        | User cancels jog                                         |
| `StartCmd`       | User requests job start                                  |
| `PauseCmd`       | User requests feed hold                                  |
| `ResumeCmd`      | User requests cycle resume                               |
| `AbortCmd`       | User requests abort (job, homing, or jog)                |
| `ResetCmd`       | User requests fault recovery (sends `$X` from FAULT, `0x18` from ESTOP) |
| `SpindleOnCmd`   | User requests spindle on (manual)                        |
| `SpindleOffCmd`  | User requests spindle off                                |
| `OverrideCmd`    | User adjusts feed/spindle/rapid override                 |
| `FileUploadCmd`  | Desktop begins file upload (`@FILE_UPLOAD NAME=...`)     |
| `UploadAbortCmd` | Desktop cancels upload (`@FILE_UPLOAD_ABORT`)            |

### From Pico hardware / services

| Event               | Source                  | Description                                   |
|---------------------|-------------------------|-----------------------------------------------|
| `HwEstopAsserted`   | `PIN_ESTOP` GPIO (GP15) | Hardware E-stop pin pulled low                |
| `HwEstopCleared`    | `PIN_ESTOP` GPIO (GP15) | Hardware E-stop pin released                  |
| `SdMounted`         | `StorageService`        | Storage mounted successfully                  |
| `SdRemoved`         | `StorageService`        | Storage removed or unmounted                  |
| `JobSelected`       | Storage / protocol      | Valid G-code file selected (sets bool only)   |
| `JobDeselected`     | Storage / protocol      | File selection cleared                        |
| `BootTimeout`       | Timer                   | Teensy did not send `@BOOT` within timeout    |
| `SyncTimeout`       | Timer                   | No `@GRBL_STATE` received after `@BOOT`       |
| `JobStreamComplete` | Streaming engine        | All lines sent and all `ok`s received         |
| `UploadComplete`    | Upload protocol handler | `@FILE_UPLOAD_END` CRC verified, file written |
| `UploadFailed`      | Upload protocol handler | CRC mismatch, SD write fail, or chunk timeout |
| `UploadAborted`     | Upload protocol handler | Desktop sent `@FILE_UPLOAD_ABORT`, or USB disconnect cleanup |

---

## Complete Transition Table

Every state × every significant event is defined. Unlisted combinations are **ignored**.

### `BOOTING`

| Event              | Next State            | Action                              |
|--------------------|-----------------------|-------------------------------------|
| `TeensyConnected`  | `SYNCING`             | Start sync timeout timer            |
| `BootTimeout`      | `TEENSY_DISCONNECTED` | —                                   |
| `HwEstopAsserted`  | `ESTOP`               | —                                   |
| `SdMounted`        | stay                  | Set `sd_mounted_`                   |
| `SdRemoved`        | stay                  | Clear `sd_mounted_`, clear `job_selected_` |

### `SYNCING`

First `@GRBL_STATE` determines the Pico's initial operation state.

| Event               | Next State            | Action                              |
|---------------------|-----------------------|-------------------------------------|
| `GrblIdle`          | `IDLE`                | —                                   |
| `GrblAlarm`         | `FAULT`               | Store alarm code                    |
| `GrblEstop`         | `ESTOP`               | —                                   |
| `GrblHoming`        | `HOMING`              | —                                   |
| `GrblCycle`         | `RUNNING`             | Teensy was mid-job on reconnect     |
| `GrblHold`          | `HOLD`                | Set `hold_complete_` from substate  |
| `GrblJog`           | `JOG`                 | —                                   |
| `GrblDoor`          | `HOLD`                | —                                   |
| `GrblToolChange`    | `HOLD`                | —                                   |
| `GrblSleep`         | `IDLE`                | —                                   |
| `SyncTimeout`       | `TEENSY_DISCONNECTED` | —                                   |
| `TeensyDisconnected`| `TEENSY_DISCONNECTED` | —                                   |
| `HwEstopAsserted`   | `ESTOP`               | —                                   |
| `SdMounted`         | stay                  | Set `sd_mounted_`                   |
| `SdRemoved`         | stay                  | Clear `sd_mounted_`, clear `job_selected_` |

### `TEENSY_DISCONNECTED`

No active job. Waiting for Teensy to come back online.

| Event              | Next State | Action                              |
|--------------------|------------|-------------------------------------|
| `TeensyConnected`  | `SYNCING`  | Start sync timeout timer            |
| `HwEstopAsserted`  | `ESTOP`    | —                                   |
| `SdMounted`        | stay       | Set `sd_mounted_`                   |
| `SdRemoved`        | stay       | Clear `sd_mounted_`, clear `job_selected_` |

### `IDLE`

| Event               | Next State | Action                                      | Guard                                         |
|---------------------|------------|---------------------------------------------|-----------------------------------------------|
| `HomeCmd`           | stay       | Send `$H` to Teensy                        | `teensy_connected_`                           |
| `JogCmd`            | stay       | Send `$J=<params>` to Teensy               | `teensy_connected_`                           |
| `StartCmd`          | `STARTING` | Set `job_session_active_`, begin streaming  | `job_selected_` ∧ `teensy_connected_` ∧ `sd_mounted_` ∧ `all_axes_homed_` |
| `SpindleOnCmd`      | stay       | Send spindle command to Teensy              | `teensy_connected_`                           |
| `SpindleOffCmd`     | stay       | Send spindle stop to Teensy                 | `teensy_connected_`                           |
| `GrblHoming`        | `HOMING`   | —                                           |                                               |
| `GrblJog`           | `JOG`      | —                                           |                                               |
| `GrblCycle`         | `RUNNING`  | —                                           |                                               |
| `GrblHold`          | `HOLD`     | Set `hold_complete_` from substate          |                                               |
| `GrblAlarm`         | `FAULT`    | Store alarm code                            |                                               |
| `GrblEstop`         | `ESTOP`    | —                                           |                                               |
| `GrblDoor`          | `HOLD`     | —                                           |                                               |
| `GrblToolChange`    | `HOLD`     | —                                           |                                               |
| `GrblSleep`         | stay       | GRBL de-energized steppers — treat as IDLE  |                                               |
| `TeensyDisconnected`| `TEENSY_DISCONNECTED` | —                                |                                               |
| `TeensyConnected`   | `SYNCING`  | Unexpected reboot — no active job to cancel |                                               |
| `HwEstopAsserted`   | `ESTOP`    | Send E-stop signal to Teensy                |                                               |
| `SdMounted`         | stay       | Set `sd_mounted_`, recompute caps           |                                               |
| `SdRemoved`         | stay       | Clear `sd_mounted_`, clear `job_selected_`, recompute caps |                              |
| `JobSelected`       | stay       | Set `job_selected_`, recompute caps         |                                               |
| `JobDeselected`     | stay       | Clear `job_selected_`, recompute caps       |                                               |
| `FileUploadCmd`     | `UPLOADING`| Set `upload_active_`, open SD file for write, respond `@OK FILE_UPLOAD_READY` | `sd_mounted_` |

> `HomeCmd` and `JogCmd` do not immediately change state. GRBL's confirmation (`GrblHoming`,
> `GrblJog`) drives the transition. If GRBL rejects the command, no state change occurs.

### `HOMING`

| Event               | Next State            | Action                                      |
|---------------------|-----------------------|---------------------------------------------|
| `GrblIdle`          | `IDLE`                | Set `all_axes_homed_`                       |
| `GrblAlarm`         | `FAULT`               | Homing failed — store alarm code            |
| `GrblEstop`         | `ESTOP`               | —                                           |
| `AbortCmd`          | stay                  | Send `0x18` to Teensy                       |
| `TeensyDisconnected`| `TEENSY_DISCONNECTED` | See note below                              |
| `HwEstopAsserted`   | `ESTOP`               | Send E-stop signal to Teensy                |
| `SdRemoved`         | stay                  | Clear `sd_mounted_`, clear `job_selected_`, recompute caps |
| `SdMounted`         | stay                  | Set `sd_mounted_`, recompute caps           |

> **Disconnect during HOMING**: the Pico enters `TEENSY_DISCONNECTED`, not `COMMS_FAULT`,
> because no job session is active. However, the Teensy may still be physically executing
> the homing cycle. The Pico cannot send a stop command until the link is restored. If this
> is a safety concern, the hardware E-stop is the correct response. On reconnect, the Pico
> syncs GRBL state via `SYNCING` and will reflect whatever state GRBL ended up in.

### `JOG`

| Event               | Next State            | Action                                      |
|---------------------|-----------------------|---------------------------------------------|
| `GrblIdle`          | `IDLE`                | Jog completed or cancelled                  |
| `GrblAlarm`         | `FAULT`               | Limit hit during jog — store alarm code     |
| `GrblEstop`         | `ESTOP`               | —                                           |
| `JogStop`           | stay                  | Send `0x85` to Teensy                       |
| `AbortCmd`          | stay                  | Send `0x18` to Teensy                       |
| `TeensyDisconnected`| `TEENSY_DISCONNECTED` | See note below                              |
| `HwEstopAsserted`   | `ESTOP`               | Send E-stop signal to Teensy                |
| `SdRemoved`         | stay                  | Clear `sd_mounted_`, clear `job_selected_`, recompute caps |
| `SdMounted`         | stay                  | Set `sd_mounted_`, recompute caps           |

> **Disconnect during JOG**: same reasoning as HOMING. No job session is active, so the
> state is `TEENSY_DISCONNECTED` rather than `COMMS_FAULT`. The jog move will decelerate
> and stop on the Teensy independently — jog moves have a built-in timeout in grblHAL and
> will not run indefinitely. Hardware E-stop remains the operator's safety fallback.

### `STARTING`

Job streaming has begun (`job_session_active_` = true). GRBL is still `STATE_IDLE`
processing non-motion preamble (G20, G90, tool setup) until the first motion command
triggers `STATE_CYCLE`.

| Event               | Next State | Action                                          |
|---------------------|------------|-------------------------------------------------|
| `GrblCycle`         | `RUNNING`  | First motion started                            |
| `GrblIdle`          | *(flags)*  | See GrblIdle table above                        |
| `GrblAlarm`         | `FAULT`    | Cancel job session, store alarm code            |
| `GrblEstop`         | `ESTOP`    | Cancel job session                              |
| `GrblDoor`          | `HOLD`     | Safety door opened during startup               |
| `GrblHold`          | `HOLD`     | Defensive — hold during preamble (edge case)    |
| `GrblToolChange`    | `HOLD`     | Defensive — tool change in preamble (edge case) |
| `GcodeError`        | stay       | Set `abort_pending_`, send `0x18`, emit `@EVENT JOB_ERROR REASON=GCODE_ERROR CODE=<n>` |
| `AbortCmd`          | stay       | Set `abort_pending_`, send `0x18` to Teensy     |
| `TeensyDisconnected`| `COMMS_FAULT` | Cancel job session, emit `@EVENT JOB_ERROR REASON=TEENSY_DISCONNECTED` |
| `HwEstopAsserted`   | `ESTOP`    | Cancel job session, send E-stop signal          |
| `SdRemoved`         | stay       | Set `abort_pending_`, send `0x18`, clear `sd_mounted_`, clear `job_selected_`, emit `@EVENT JOB_ERROR REASON=SD_REMOVED` |
| `SdMounted`         | stay       | Set `sd_mounted_`                               |
| `JobStreamComplete` | stay       | Set `job_stream_complete_`                      |

### `RUNNING`

Job active (`job_session_active_` = true). GRBL is in `STATE_CYCLE`, or briefly
`STATE_IDLE` between motion segments. The Pico continues streaming G-code lines from storage.

| Event               | Next State    | Action                                          |
|---------------------|---------------|-------------------------------------------------|
| `GrblHold`          | `HOLD`        | Set `hold_complete_` from substate              |
| `GrblIdle`          | *(flags)*     | See GrblIdle table above                        |
| `GrblAlarm`         | `FAULT`       | Cancel job session, store alarm code            |
| `GrblEstop`         | `ESTOP`       | Cancel job session                              |
| `GrblDoor`          | `HOLD`        | Safety door opened during job                   |
| `GrblToolChange`    | `HOLD`        | M6 encountered — manual tool change             |
| `GcodeError`        | stay          | Set `abort_pending_`, send `0x18`, emit `@EVENT JOB_ERROR REASON=GCODE_ERROR CODE=<n>` |
| `PauseCmd`          | stay          | Send `!` (CMD_FEED_HOLD) to Teensy              |
| `AbortCmd`          | stay          | Set `abort_pending_`, send `0x18` to Teensy     |
| `SpindleOnCmd`      | stay          | Send spindle command to Teensy                  |
| `SpindleOffCmd`     | stay          | Send spindle stop to Teensy                     |
| `OverrideCmd`       | stay          | Forward override bytes to Teensy                |
| `TeensyDisconnected`| `COMMS_FAULT` | Cancel job session, emit `@EVENT JOB_ERROR REASON=TEENSY_DISCONNECTED` |
| `HwEstopAsserted`   | `ESTOP`       | Cancel job session, send E-stop signal          |
| `SdRemoved`         | stay          | Set `abort_pending_`, send `0x18`, clear `sd_mounted_`, clear `job_selected_`, emit `@EVENT JOB_ERROR REASON=SD_REMOVED` |
| `SdMounted`         | stay          | Set `sd_mounted_`                               |
| `JobStreamComplete` | stay          | Set `job_stream_complete_`                      |

### `HOLD`

Motion paused. Caused by feed hold (`!`), safety door, or tool change.
Job session may or may not be active.

| Event               | Next State    | Action                                          |
|---------------------|---------------|-------------------------------------------------|
| `GrblCycle`         | `RUNNING`     | Resumed                                         |
| `GrblHold`          | stay          | Update `hold_complete_` from substate           |
| `GrblIdle`          | *(see below)* |                                                 |
| `GrblAlarm`         | `FAULT`       | Cancel job session if active, store alarm code  |
| `GrblEstop`         | `ESTOP`       | Cancel job session if active                    |
| `ResumeCmd`         | stay          | Send `~` (CMD_CYCLE_START) to Teensy            |
| `AbortCmd`          | stay          | Set `abort_pending_`, send `0x18` to Teensy     |
| `SpindleOnCmd`      | stay          | Send spindle command to Teensy                  |
| `SpindleOffCmd`     | stay          | Send spindle stop to Teensy                     |
| `OverrideCmd`       | stay          | Forward override bytes to Teensy                |
| `TeensyDisconnected`| *(conditional)* | If `job_session_active_`: → `COMMS_FAULT`, emit `@EVENT JOB_ERROR REASON=TEENSY_DISCONNECTED`. Else: → `TEENSY_DISCONNECTED` |
| `HwEstopAsserted`   | `ESTOP`       | Cancel job session if active, send E-stop signal|
| `SdRemoved`         | stay          | If `job_session_active_`: set `abort_pending_`, send `0x18`, emit `@EVENT JOB_ERROR REASON=SD_REMOVED`. Always clear `sd_mounted_`, clear `job_selected_`, recompute caps |
| `SdMounted`         | stay          | Set `sd_mounted_`, recompute caps           |

> `ResumeCmd` is only accepted when `hold_complete_` is true (GRBL finished decelerating).
> The `JOB_RESUME` cap reflects this — it is 0 while hold is still pending.
>
> **Safety door note**: if the hold was caused by a safety door (`GrblDoor`), GRBL will
> internally reach `hold_complete_` once parking retract finishes — but it will still ignore
> `~` (cycle start) until the door is physically closed. `JOB_RESUME` will appear enabled
> on the UI while the door is open. This is safe: GRBL ignores the premature `~` and waits.
> For machines with a door sensor, a `door_open_` internal flag and a `GrblDoorClosed` event
> could gate `JOB_RESUME` more precisely, but this machine has no door sensor.

**GrblIdle during HOLD:**

| `job_session_active_` | `abort_pending_` | `job_stream_complete_` | → State     | Action |
|-----------------------|------------------|------------------------|-------------|--------|
| false                 | —                | —                      | `IDLE`      | No session — normal hold released or GRBL reset independently |
| true                  | true             | —                      | `IDLE`      | Abort confirmed, clear session |
| true                  | false            | true                   | `IDLE`      | Job complete, emit `@EVENT JOB_COMPLETE`, clear session |
| true                  | false            | false                  | stay `HOLD` | GrblIdle unexpected — remain in HOLD, wait for GrblCycle after resume |

> The last row: if GRBL goes `IDLE` while the Pico is in `HOLD` and no abort or completion
> is pending, something unusual happened (timing artefact, GRBL edge case). The correct
> response is to remain in `HOLD` — do not silently transition to `RUNNING`. The next
> `GrblCycle` after a `ResumeCmd` will drive the transition to `RUNNING`.

### `FAULT`

GRBL is in `STATE_ALARM`. Machine locked. `$X` is the standard recovery path.

| Event               | Next State    | Action                                          |
|---------------------|---------------|-------------------------------------------------|
| `GrblIdle`          | `IDLE`        | Alarm cleared                                   |
| `GrblAlarm`         | stay          | Update alarm code (new alarm after partial reset) |
| `GrblEstop`         | `ESTOP`       | —                                               |
| `ResetCmd`          | stay          | Send `$X` to Teensy — wait for `GrblIdle`       |
| `TeensyDisconnected`| `TEENSY_DISCONNECTED` | —                                     |
| `TeensyConnected`   | `SYNCING`     | Unexpected reboot                               |
| `HwEstopAsserted`   | `ESTOP`       | —                                               |
| `SdMounted`         | stay          | Set `sd_mounted_`, recompute caps               |
| `SdRemoved`         | stay          | Clear `sd_mounted_`, clear `job_selected_`, recompute caps |

> **Standard recovery**: operator sends `ResetCmd` → Pico sends `$X` → GRBL transitions
> to `STATE_IDLE` → `GrblIdle` → Pico enters `IDLE`.
>
> **Last resort**: if `$X` does not clear the condition (e.g. structural GRBL failure),
> the operator may power-cycle the Teensy. The Pico will handle the resulting
> `TeensyDisconnected` / `TeensyConnected` cycle.

### `COMMS_FAULT`

The UART link to the Teensy was lost while a job was active. The Teensy may still be
running. Motion state is unknown. Recovery requires reconnect, not `$X`.

| Event               | Next State | Action                                          |
|---------------------|------------|-------------------------------------------------|
| `TeensyConnected`   | `SYNCING`  | Reconnected — sync GRBL state before doing anything |
| `HwEstopAsserted`   | `ESTOP`    | —                                               |
| `SdMounted`         | stay       | Set `sd_mounted_`                               |
| `SdRemoved`         | stay       | Clear `sd_mounted_`, clear `job_selected_`      |

> **Post-reconnect policy**: after a comms loss, the Pico resynchronizes observable machine
> state only. It does not attempt to reconstruct or resume the old stream session.
> `job_session_active_`, `job_stream_complete_`, and `abort_pending_` are all cleared on
> entry to `COMMS_FAULT`. If GRBL is still in `STATE_CYCLE` when the link is restored,
> the Pico will reflect `RUNNING` — but with no session context. When GRBL eventually goes
> idle, the Pico enters `IDLE` with no `JOB_COMPLETE` event (the outcome of the job is
> unknown). Resuming the old stream from an arbitrary mid-file position risks sending
> duplicate or out-of-sequence G-code; the operator must start a new job from the beginning.
>
> The Pico cannot send any commands while in `COMMS_FAULT`. The hardware E-stop is the
> correct intervention if the machine must be stopped immediately. On reconnect, the Pico
> enters `SYNCING` and reflects whatever GRBL state it finds.

### `ESTOP`

Hardware E-stop latched. GRBL is in `STATE_ESTOP` and/or Pico's `PIN_ESTOP` is asserted.

| Event               | Next State | Action                                          |
|---------------------|------------|-------------------------------------------------|
| `HwEstopCleared`    | stay       | Set `hw_estop_active_ = false`, recompute caps  |
| `GrblIdle`          | `IDLE`     | E-stop fully cleared, GRBL confirmed idle       |
| `GrblAlarm`         | `FAULT`    | E-stop cleared but GRBL alarmed — use `$X`      |
| `ResetCmd`          | stay       | If `!hw_estop_active_`: send `0x18`, wait for GRBL. If pin still held: `@ERROR ESTOP_PIN_ACTIVE` |
| `TeensyDisconnected`| `TEENSY_DISCONNECTED` | —                                |
| `TeensyConnected`   | `SYNCING`  | Unexpected reboot                               |
| `SdMounted`         | stay       | Set `sd_mounted_`                               |
| `SdRemoved`         | stay       | Clear `sd_mounted_`, clear `job_selected_`      |

> **Recovery sequence**: (1) Release physical E-stop → `HwEstopCleared`. (2) `ResetCmd`
> → Pico sends `0x18`. (3) GRBL `mc_reset()` → `STATE_IDLE`. (4) `GrblIdle` → `IDLE`.
> If GRBL goes to `STATE_ALARM` instead, Pico enters `FAULT` for standard alarm recovery.

### `UPLOADING`

Desktop file transfer in progress. Entered from `IDLE` when `@FILE_UPLOAD` is received and
accepted. All action capability flags are 0. The GRBL/Teensy link is unaffected — the Pico
continues to monitor Teensy state passively, but no motion commands are issued.

| Event               | Next State | Action                                                              |
|---------------------|------------|---------------------------------------------------------------------|
| `UploadComplete`    | `IDLE`     | Clear `upload_active_`, recompute caps, emit `@EVENT UPLOAD_COMPLETE NAME=<filename>` |
| `UploadFailed`      | `IDLE`     | Clear `upload_active_`, delete partial file, recompute caps, emit `@EVENT UPLOAD_FAILED REASON=<reason>` |
| `UploadAbortCmd`    | `IDLE`     | Clear `upload_active_`, delete partial file, recompute caps, respond `@OK FILE_UPLOAD_ABORT` |
| `HwEstopAsserted`   | `ESTOP`    | Clear `upload_active_`, delete partial file, send E-stop signal to Teensy |
| `GrblAlarm`         | `FAULT`    | Clear `upload_active_`, delete partial file, store alarm code, emit `@EVENT UPLOAD_FAILED REASON=GRBL_ALARM` |
| `GrblEstop`         | `ESTOP`    | Clear `upload_active_`, delete partial file                               |
| `TeensyConnected`   | `SYNCING`  | Clear `upload_active_`, delete partial file, emit `@EVENT UPLOAD_FAILED REASON=TEENSY_REBOOT` — Teensy rebooted mid-upload; must re-sync before any operation |
| `SdRemoved`         | `IDLE`     | Clear `upload_active_`, clear `sd_mounted_`, delete partial file (already gone), recompute caps, emit `@EVENT UPLOAD_FAILED REASON=SD_REMOVED` |

> **E-stop during upload**: the partial file is deleted immediately. The Pico does not
> attempt to preserve it. After recovery to `IDLE`, the operator must re-upload from scratch.
>
> **USB disconnect during upload**: the upload protocol handler detects the disconnect, fires
> `UploadAborted`, deletes the partial file, and the state machine returns to `IDLE`.
> No explicit client-side cleanup is needed — the session is gone.
>
> **Storage removal during upload**: `SdRemoved` transitions directly to `IDLE`. The partial
> file is gone with the storage, so no delete is needed. The `SD_WRITE_FAIL` chunk error
> path (`UploadFailed`) may race with `SdRemoved` — whichever arrives first terminates the
> upload; the second is ignored since `upload_active_` is already clear.
>
> **Teensy state passthrough**: `GrblAlarm` and `GrblEstop` arriving during `UPLOADING` are
> handled as they would be in `IDLE` (→ `FAULT` and `ESTOP` respectively). Upload is aborted.
> Any other `@GRBL_STATE` messages arriving during upload are recorded but do not change
> Pico operation state.

---

## Operator Command → Teensy Action Mapping

| Operator command | Pico sends to Teensy               | GRBL command type             |
|------------------|------------------------------------|-------------------------------|
| `HomeCmd`        | `$H\n`                             | System command                |
| `JogCmd`         | `$J=G91 G21 X.. Y.. F..\n`        | System command                |
| `JogStop`        | `0x85` (single byte)               | Realtime command              |
| `StartCmd`       | G-code lines from storage          | G-code stream                 |
| `PauseCmd`       | `!` / `0x21` (single byte)        | Realtime command              |
| `ResumeCmd`      | `~` / `0x7E` (single byte)        | Realtime command              |
| `AbortCmd`       | `0x18` (single byte)               | Realtime (soft reset)         |
| `ResetCmd`       | `$X\n` from FAULT, `0x18` from ESTOP | System / Realtime           |
| `SpindleOnCmd`   | `M3 S<rpm>\n`                      | G-code                        |
| `SpindleOffCmd`  | `M5\n`                             | G-code                        |
| `OverrideCmd`    | Override bytes (0x91–0x9D)         | Realtime command              |

---

## Capability Flags (`@CAPS`)

Computed after every state transition. Sent alongside `@STATE` to all clients.
The desktop binds these directly to UI controls — no local logic or state derivation.

| Flag          | Condition                                                             | Controls                              |
|---------------|-----------------------------------------------------------------------|---------------------------------------|
| `MOTION`      | `IDLE`                                                                | Jog, home, zero axes                  |
| `PROBE`       | `IDLE` ∧ `all_axes_homed_`                                           | Z-probe / tool length                 |
| `SPINDLE`     | `IDLE` or `RUNNING` or `HOLD`                                        | Spindle on/off                        |
| `FILE_LOAD`   | `IDLE` ∧ `sd_mounted_`                                               | Load or replace active job file       |
| `JOB_START`   | `IDLE` ∧ `job_loaded_` ∧ `teensy_connected_` ∧ `sd_mounted_` ∧ `all_axes_homed_` | Start job              |
| `JOB_PAUSE`   | `RUNNING`                                                             | Pause job                             |
| `JOB_RESUME`  | `HOLD` ∧ `hold_complete_`                                            | Resume (only after deceleration done) |
| `JOB_ABORT`   | `RUNNING` or `HOLD` or `STARTING`                                    | Abort job                             |
| `OVERRIDES`   | `RUNNING` or `HOLD`                                                   | Feed, spindle, rapid override sliders |
| `RESET`       | `FAULT` or (`ESTOP` ∧ `!hw_estop_active_`)                          | Unlock / soft reset button            |

> All caps are 0 in `UPLOADING`. No machine commands may be issued while a file transfer
> is in progress.

> `MOTION` and `PROBE` have different guards: all motion is available from `IDLE`, but
> probing additionally requires `all_axes_homed_` since probe results are meaningless
> without a known machine origin. Home and jog do not share this restriction.

```cpp
Caps MachineStateMachine::compute_caps() const {
    return {
        .motion      = state_ == IDLE,
        .probe       = state_ == IDLE && all_axes_homed_,
        .spindle     = state_ == IDLE || state_ == RUNNING || state_ == HOLD,
        .file_load   = state_ == IDLE && sd_mounted_,
        .job_start   = state_ == IDLE && job_loaded_ && teensy_connected_ && sd_mounted_ && all_axes_homed_,
        .job_pause   = state_ == RUNNING,
        .job_resume  = state_ == HOLD && hold_complete_,
        .job_abort   = state_ == RUNNING || state_ == HOLD || state_ == STARTING,
        .overrides   = state_ == RUNNING || state_ == HOLD,
        .reset       = state_ == FAULT || (state_ == ESTOP && !hw_estop_active_),
    };
}
```

---

## Protocol Output

Every operation state transition emits:
```
@STATE <state>
@CAPS MOTION=<0|1> PROBE=<0|1> SPINDLE=<0|1> FILE_LOAD=<0|1> JOB_START=<0|1> JOB_PAUSE=<0|1> JOB_RESUME=<0|1> JOB_ABORT=<0|1> OVERRIDES=<0|1> RESET=<0|1>
```

Safety level changes emit:
```
@SAFETY <level>
```

Significant events emit:
```
@EVENT JOB_COMPLETE
@EVENT JOB_ERROR REASON=<TEENSY_DISCONNECTED|SD_REMOVED|GRBL_ALARM|GCODE_ERROR>
@EVENT ALARM CODE=<n> MSG=<text>
@EVENT SD_MOUNTED
@EVENT SD_REMOVED
@EVENT TEENSY_CONNECTED
@EVENT TEENSY_DISCONNECTED
```

### Example sequences

**Startup — GRBL was alarmed from a previous session:**
```
@STATE BOOTING
@STATE SYNCING
@GRBL_STATE ALARM CODE=2        ← Teensy reports hard limit alarm
@STATE FAULT
@CAPS ... RESET=1
@EVENT ALARM CODE=2 MSG=HARD_LIMIT
← operator: ResetCmd
→ Teensy: $X
@GRBL_STATE IDLE
@STATE IDLE
@CAPS MOTION=1 ...
```

**Job run → pause → resume → complete:**
```
← operator: StartCmd
@STATE STARTING
@CAPS ... JOB_ABORT=1
@SAFETY MONITORING
@GRBL_STATE CYCLE               ← first motion
@STATE RUNNING
@CAPS ... JOB_PAUSE=1 JOB_ABORT=1 OVERRIDES=1
← operator: PauseCmd
→ Teensy: ! (CMD_FEED_HOLD)
@GRBL_STATE HOLD SUBSTATE=PENDING
@STATE HOLD
@CAPS ... JOB_RESUME=0 JOB_ABORT=1    ← decelerating, can't resume yet
@GRBL_STATE HOLD SUBSTATE=COMPLETE
@CAPS ... JOB_RESUME=1 JOB_ABORT=1    ← stopped, can resume
← operator: ResumeCmd
→ Teensy: ~ (CMD_CYCLE_START)
@GRBL_STATE CYCLE
@STATE RUNNING
@CAPS ... JOB_PAUSE=1 ...
@EVENT JOB_COMPLETE
@STATE IDLE
@CAPS MOTION=1 ...
@SAFETY SAFE
```

**Job abort:**
```
← operator: AbortCmd
→ Teensy: 0x18 (CMD_RESET / soft reset)
@GRBL_STATE IDLE                ← mc_reset() goes directly to IDLE
@STATE IDLE                     ← abort_pending_ was true → job session cleared
@CAPS MOTION=1 ...
@SAFETY SAFE
```

**E-stop → recovery:**
```
← HwEstopAsserted (GPIO)
@STATE ESTOP
@CAPS ... RESET=0               ← pin still held
@SAFETY CRITICAL
← HwEstopCleared (GPIO)
@CAPS ... RESET=1               ← pin released, operator can now reset
← operator: ResetCmd
→ Teensy: 0x18 (soft reset)
@GRBL_STATE IDLE
@STATE IDLE
@CAPS MOTION=1 ...
@SAFETY SAFE
```

**Comms lost during job:**
```
(in RUNNING)
← TeensyDisconnected (UART timeout)
@STATE COMMS_FAULT
@CAPS ... (all 0)
@EVENT JOB_ERROR REASON=TEENSY_DISCONNECTED
← (operator reconnects Teensy)
← TeensyConnected (@BOOT TEENSY_READY)
@STATE SYNCING
@GRBL_STATE IDLE                ← Teensy completed/reset while disconnected
@STATE IDLE
```

---

## Teensy Protocol Requirements

`pico_framework.cpp` must be redesigned to support:

### 1. GRBL state push

Poll `state_get()` and `sys.holding_state` every task cycle. When either changes, emit:
```
@GRBL_STATE <state> [SUBSTATE=<PENDING|COMPLETE>] [CODE=<n>]
```
States: `IDLE`, `ALARM`, `CHECK`, `HOMING`, `CYCLE`, `HOLD`, `JOG`, `DOOR`,
`SLEEP`, `ESTOP`, `TOOL_CHANGE`. Include `CODE=<n>` from `sys.alarm` when state is `ALARM`.

### 2. G-code streaming with flow control

Accept raw G-code lines (no `@` prefix) and feed to GRBL via `protocol_enqueue_gcode()`.
Relay GRBL's `ok` / `error:N` back to the Pico for each line. The Pico uses these to
track stream completion and implement character-count flow control.

### 3. Realtime command forwarding

Accept single-byte realtime commands and enqueue via `protocol_enqueue_realtime_command()`:
- `!` (0x21) → `CMD_FEED_HOLD`
- `~` (0x7E) → `CMD_CYCLE_START`
- `0x18` → `CMD_RESET`
- `0x85` → jog cancel
- Override bytes (0x90–0x9F range)

### 4. System command forwarding

Accept `$H`, `$X`, `$J=...` and route to `system_execute_line()`.

---

## Separate Concerns

### StorageState
Managed by `StorageService`. Independent of machine operation. Notifies state machine
on storage mount/unmount so `FILE_LOAD` and `JOB_START` caps stay accurate.

### UI Preview Selection
Desktop and TFT preview selection are separate UI state. They are not represented in
`MachineFsm` and are not sent over the protocol. The Pico only owns the currently loaded
job file.
The loaded job is persisted by filename in Pico flash and may be restored after boot or
SD remount if that filename still exists in the refreshed file list.
### Jog UI Preferences
Step size and feed rate selection are purely UI state on the touch screen.
Not part of the state machine. Not reported over the protocol.

### Position / DRO
Machine position is a separate data stream, not a state machine concern. The Pico
periodically polls the Teensy for position data and relays it to clients. Frequency
scales with activity.

---

## What Needs to Be Built

| Item | Status |
|------|--------|
| Unified `MachineStateMachine` with 13 operation states | ❌ Not built |
| Safety level tracking inside state machine | ❌ Not built |
| Internal flags (`job_session_active_`, `abort_pending_`, `hold_complete_`, etc.) | ❌ Not built |
| `compute_caps()` method with 10 flags | ❌ Not built |
| `@STATE` + `@CAPS` + `@SAFETY` + `@EVENT` emission | ❌ Not built |
| Teensy `pico_framework.cpp` rewrite (`@GRBL_STATE` push, streaming, realtime forwarding) | ❌ Not built |
| Pico UART1 driver (GP20/GP21, 115200) | ❌ Not built |
| Pico USB CDC-ACM driver for desktop link | ❌ Not built |
| Pico protocol dispatcher | ❌ Not built |
| Pico G-code streaming engine (SD → Teensy with `ok`/`error` flow control) | ❌ Not built |
| `StorageService` → FSM notifications | ❌ Not built |
| `PIN_ESTOP` GPIO (GP15) → `HwEstopAsserted` / `HwEstopCleared` | ❌ Not built |
| Desktop `PicoProtocolService` | ❌ Not built |
| Desktop `MainWindowViewModel` rework (pure `@STATE`/`@CAPS` sink) | ❌ Not built |
| Desktop `ConnectViewModel` rework (`@PING`/`@INFO` handshake) | ❌ Not built |
| Desktop `ManualControlViewModel` rework (`@`-commands only) | ❌ Not built |
| Desktop `DiagnosticsViewModel` rework | ❌ Not built |
| Desktop `Enums.cs` cleanup (display-only, no state writes) | ❌ Not built |
| Teensy `my_stream.cpp` / `my_stream.h` deleted | ❌ Not built |
