# Plan: Full Binary Framing for Desktop ↔ Pico

## Current state

- Frame types 1–4 exist for file transfer (upload/download data + acks)
- All other communication uses `@COMMAND KEY=VALUE\n` text over the same USB CDC link
- Both sides already have the COBS + CRC-32 + wire-escape infrastructure

---

## Proposed wire format extension

Extend the frame type enum with three new top-level types:

| Type  | Value | Direction       | Description                  |
|-------|-------|-----------------|------------------------------|
| CMD   | 5     | Desktop → Pico  | Command request              |
| RESP  | 6     | Pico → Desktop  | Response to a command        |
| EVENT | 7     | Pico → Desktop  | Unsolicited event/state push |

Reuse the existing frame header as-is:
```
[type: u8][transfer_id: u8][flags: u8][seq: u32][payload_len: u16][payload][crc: u32]
```
- `seq` becomes a **request ID** for CMD frames — RESP echoes it back for correlation
- `transfer_id = 0` for all non-transfer frames

Each CMD/RESP/EVENT payload starts with a `message_type: u8` sub-type, followed by a fixed struct per message type.

---

## Phase 1 — Define protocol types (no behavior change) — COMPLETE

Status: complete as of 2026-04-20.

Create two new files:
- [x] `pico2W/src/protocol/protocol_defs.h` — C packed structs + enums for all message types
- [x] `desktop/Protocol/ProtocolDefs.cs` — C# equivalents (matching byte layout)

Implemented coverage:
- [x] Frame types 1–7, including `CMD = 5`, `RESP = 6`, `EVENT = 7`
- [x] 30 command payload structs
- [x] 22 response payload structs
- [x] 19 event/state-push payload structs
- [x] Shared enums/flags for axes, capabilities, machine state, safety level, storage operation, and protocol errors
- [x] Fixed-size buffers for wire strings such as filenames, firmware/board names, reasons, and messages
- [x] Layout guards for the frame header, `CmdJog`, and `RespPos`
- [x] No behavior changes; neither side sends or parses CMD/RESP/EVENT frames yet

Verification:
- [x] `dotnet build desktop\desktop.csproj` completed with 0 compile errors. It emitted an unrelated apphost copy warning because a running `PortableCncApp.exe` process had the executable locked.
- [x] `protocol_defs.h` passed C11 and C++17 syntax checks with GCC/G++.

Covers all ~25 command types, ~20 response types, and ~10 event types as typed structs. Example:

```c
// CMD_JOG payload
typedef struct __attribute__((packed)) {
    uint8_t  message_type;  // CMD_JOG = 6
    uint8_t  axis;          // 0=X, 1=Y, 2=Z
    float    dist;
    uint16_t feed;
} CmdJog;

// RESP_POS payload
typedef struct __attribute__((packed)) {
    uint8_t  message_type;  // RESP_POS = 7
    uint32_t request_seq;
    float    mx, my, mz;
    float    wx, wy, wz;
} RespPos;
```

---

## Phase 2 — Pico: receive binary commands — COMPLETE

Status: complete as of 2026-04-20.

In `desktop_protocol.cpp`, add binary CMD frame dispatch alongside existing text dispatch:
- [x] Read `message_type` from frame payload
- [x] Copy payload bytes into the appropriate packed struct
- [x] Call the same underlying logic that text handlers call today
- [x] Keep existing text dispatch active
- [x] Keep existing text responses/events active until Phase 3
- [x] Ignore binary compatibility placeholders with no current text handler (`PROBE_Z`, `BEGIN_JOB`, `END_JOB`, `CLEAR_JOB`)

Verification:
- [x] `cmake --build pico2W\build` completed successfully.

---

## Phase 3 — Pico: emit binary responses and events — COMPLETE

Status: complete as of 2026-04-20.

Replace `emit_ok()`, `emit_error()`, `emit_state_update()`, `emit_pos()`, etc. with binary frame emitters that build typed payloads and call `transport_.send_frame()`.

Implemented:
- [x] `emit_state()`, `emit_caps()`, `emit_safety()`, `emit_job()`, and `emit_position()` now emit typed RESP frames while handling a command and typed EVENT frames for unsolicited pushes
- [x] `emit_event()` / `emit_event_kv()` now emit typed EVENT frames for current event names
- [x] `emit_ok()` / `emit_ok_kv()` now emit typed RESP frames
- [x] `emit_error()` / `emit_storage_error()` now emit typed RESP error frames
- [x] File-list rows/end and file-transfer session ready/complete/abort responses now use typed RESP frames
- [x] Upload/download chunk data and ACK frames remain frame types 1–4
- [x] Text command receive path still exists, but outbound desktop protocol messages are no longer `@...` lines

Verification:
- [x] `cmake --build pico2W\build` completed successfully.
- [x] No `send_fmt()` / `send_line()` calls remain in `pico2W/src/protocol/desktop_protocol.cpp`.

---

## Phase 4 — Desktop: send binary commands — COMPLETE

Status: complete as of 2026-04-20.

In `PicoProtocolService.cs`, replace `Send("@JOG AXIS=X ...")` style calls with frame-building helpers that write typed structs into a CMD frame and pass to `SerialService.SendFrame()`.

Implemented:
- [x] Added CMD frame sending through existing `SerialService.SendFrame()`
- [x] Added monotonically increasing request IDs in the frame `seq` field
- [x] Replaced public text command senders with typed command payload structs
- [x] Added fixed-buffer UTF-8 writing for filename-bearing commands
- [x] Kept upload/download data frames and transfer ACK frames on existing frame types 1–4
- [x] Removed `Send("@...")` usage from `PicoProtocolService.cs`

Verification:
- [x] `dotnet build desktop\desktop.csproj` completed successfully.

---

## Phase 5 — Desktop: parse binary responses and events — COMPLETE

In `PicoProtocolService.cs`, move logic from `OnLineReceived()` to `OnFrameReceived()` for RESP and EVENT frames. Parse `message_type`, deserialize struct, fire the same typed C# events that ViewModels already subscribe to — no ViewModel changes needed.

Status: complete as of 2026-04-20.

Implemented:
- [x] Added RESP (`6`) and EVENT (`7`) handling to `OnFrameReceived()`
- [x] Added packed-struct deserialization for binary payloads
- [x] Routed binary state/caps/safety/job/position messages into existing typed events
- [x] Routed command acknowledgements back into existing `OkReceived` tokens used by `SendCommandAndWaitAsync()`
- [x] Routed file-list, upload, download, delete, wait, and storage-error responses into the existing file-transfer events
- [x] Routed unsolicited EVENT frames into the existing `EventReceived` names and metadata dictionaries
- [x] Kept transfer data and ACK frames on existing frame types 1–4
- [x] Kept the legacy text parser in place until Phase 6

Verification:
- [x] `dotnet build desktop\desktop.csproj` completed successfully with 0 warnings and 0 errors.

---

## Phase 6 — Remove text protocol — COMPLETE

Once binary is validated end-to-end:
- [x] Remove `dispatch()` text parser on Pico
- [x] Remove `OnLineReceived()` text parser on Desktop
- [x] Remove `@`-command sending on Desktop

Status: complete as of 2026-04-20.

Implemented:
- [x] Pico no longer dispatches line packets to the desktop protocol text parser
- [x] Removed the Pico `DesktopProtocol::dispatch(const char*)` text command router
- [x] Removed Pico `UsbCdcTransport::send_line()` / `send_fmt()` text emit helpers
- [x] Desktop `PicoProtocolService` no longer subscribes to `SerialService.LineReceived`
- [x] Desktop raw text command sending through `SerialService.SendCommand()` was removed
- [x] Manual-control custom raw commands now fail closed instead of writing text to USB CDC
- [x] Binary CMD/RESP/EVENT frames and transfer frames 1-4 remain active

Verification:
- [x] `dotnet build desktop\desktop.csproj` completed successfully with 0 warnings and 0 errors.
- [x] `cmake --build pico2W\build` completed successfully.

---

## Files touched

| File | Change |
|------|--------|
| `pico2W/src/protocol/protocol_defs.h` | New — all message structs/enums |
| `desktop/Protocol/ProtocolDefs.cs` | New — C# equivalents |
| `pico2W/src/protocol/usb_cdc_transport.h/.cpp` | Add CMD/RESP/EVENT frame type constants |
| `pico2W/src/protocol/desktop_protocol.h/.cpp` | Binary dispatch + binary emission |
| `desktop/Services/SerialService.cs` | Frame builder helpers for CMD frames |
| `desktop/Services/PicoProtocolService.cs` | Replace text send/parse with binary |

ViewModels, file transfer FSM, and storage layer are untouched.
