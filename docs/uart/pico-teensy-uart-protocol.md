# Pico 2W <-> Teensy 4.1 UART Protocol

## Current Wire Contract

- Physical link: UART, 3.3 V TTL, `115200 8N1`, no flow control.
- Wiring: Pico `GP20` TX -> Teensy RX1, Pico `GP21` RX <- Teensy TX1, common GND.
- Framing: newline-terminated ASCII lines. Carriage returns are ignored.
- Ownership: Pico owns UI, SD/job metadata, desktop protocol, and safety/capability state. Teensy owns grblHAL motion execution, machine state, and machine position.

## Teensy -> Pico Messages

```text
@BOOT TEENSY_READY
@PONG
@GRBL_STATE IDLE
@GRBL_STATE HOLD SUBSTATE=0
@GRBL_STATE HOLD SUBSTATE=1
@POS MX=0.000 MY=0.000 MZ=0.000 WX=0.000 WY=0.000 WZ=0.000
ok
error:<code>
```

State values are:

- `IDLE`
- `ALARM`
- `HOMING`
- `CYCLE`
- `HOLD`
- `JOG`
- `ESTOP`
- `DOOR`
- `TOOL_CHANGE`
- `SLEEP`

`SUBSTATE=0` means hold is pending. `SUBSTATE=1` means hold is complete and resume is allowed.

`ok` and `error:<code>` are returned for accepted/rejected motion-link commands, including each `@GCODE` line.

## Pico -> Teensy Commands

```text
@PING
@HOME
@JOG AXIS=X DIST=1.000 FEED=500
@JOG_CANCEL
@GCODE G1 X10 F500
@RT_FEED_HOLD
@RT_CYCLE_START
@RT_RESET
@RT_ESTOP
@UNLOCK
@SPINDLE_ON RPM=12000
@SPINDLE_OFF
@ZERO AXIS=ALL
@ZERO AXIS=X
```

Command behavior:

- `@HOME` executes `$H`.
- `@JOG` builds `$J=G91 G21 <axis><dist> F<feed>`.
- `@JOG_CANCEL` enqueues grblHAL jog cancel.
- `@GCODE` queues one G-code line.
- realtime commands enqueue grblHAL feed hold, cycle start, reset, or E-stop/reset.
- `@UNLOCK` executes `$X`.
- `@SPINDLE_ON` queues `M3 S<rpm>`.
- `@SPINDLE_OFF` queues `M5`.
- `@ZERO` queues `G10 L20 P1 ...0` for the requested axis or all axes.

## Streaming Rules

- Pico opens the selected SD file and reads it through the core-1 worker.
- Pico sends one `@GCODE` line at a time.
- Pico waits for `ok` before advancing the stream.
- If Teensy reports `error:<code>`, Pico sends `@RT_RESET`, emits a desktop `JOB_ERROR`, and moves through abort/fault handling.
- During hold, Pico does not send additional `@GCODE` lines until motion resumes.
- Pico emits desktop binary `JOB_PROGRESS`, `JOB_COMPLETE`, and `JOB_ERROR` events; those events are not sent on this UART link.

## Legacy Protocol

The older `@HELLO`, `@CMD SEQ=... OP=...`, `@ACK`, `@ERR`, `@EVT`, `JOB_BEGIN`, `JOB_END`, and raw unprefixed job-line upload protocol has been removed from the active firmware path.
