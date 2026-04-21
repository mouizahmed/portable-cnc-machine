# Pico Storage Transfer FSM

## Purpose

The storage transfer FSM owns SD-card file operation state on the Pico. It is separate from:

- `MachineFsm`, which owns machine/motion/safety state.
- `StorageState`, which owns SD mount health (`Uninitialized`, `Mounting`, `Mounted`, etc.).
- Desktop UI preview state, which is client-side only.

The transfer FSM exists to make file operations deterministic: no stale sessions, no implicit busy state, one abort path, and structured errors for upload/download/list/load/delete.

Current implementation status: **complete**. `StorageTransferStateMachine` owns all transfer state (session ID, filename, expected size, bytes written/sent, sequences, running CRC, FatFS handle, completion record, last error). `DesktopProtocol` holds it as `transfer_` and delegates all session state through it. The upload pipeline queue and core-1 worker remain inline in `DesktopProtocol` as implementation detail; they do not constitute state ownership.

## State Model

| State | Meaning |
|---|---|
| `Idle` | No active file operation. New storage requests may be accepted if the machine/storage gates allow them. |
| `Listing` | Enumerating SD files for desktop file list. |
| `Loading` | Loading/selecting a file as the active job file. |
| `Deleting` | Deleting an SD file. |
| `UploadOpen` | Upload request accepted, target file is being opened/prepared. |
| `Uploading` | Upload session active; chunk frames are being received and written. |
| `UploadFinalizing` | Upload data received; file is being closed and CRC/size are being verified. |
| `DownloadOpen` | Download/preview request accepted, source file is being opened/prepared. |
| `Downloading` | Download session active; chunk frames are being sent and ACKed. |
| `Aborting` | Abort requested; active file/session cleanup is in progress. |
| `Faulted` | Transfer FSM detected an unrecovered storage/protocol fault. Cleanup or reset is required before accepting new transfer work. |

## ASCII diagram

Session-style operations (upload/download) progress left-to-right; `Aborting` and `Faulted` are global escape paths from the contract table.

```
                         ┌───────────────────────────────────────┐
                         │                 Idle                  │
                         └───┬─────────┬─────────┬──────────┬────┘
              FileListReq   │         │         │          │ FileDeleteReq
                             │ FileLoad│FileUnload         │
                             │         │         │          ▼
                             │         │         │    ┌──────────┐
                             │         │         │    │ Deleting │──OpComplete──► Idle
                             │         │         │    └──────────┘
                             │         │         │
                             │         │         ▼
                             │         │   ┌─────────┐
                             │         └──► Loading │──OpComplete──► Idle
                             │             └─────────┘
                             ▼
                      ┌──────────┐
                      │ Listing  │──OpComplete──► Idle
                      └──────────┘


  Idle ──UploadRequested──► ┌────────────┐
                              │ UploadOpen │
                              └─────┬──────┘
                                    │ OpComplete (file ready → emit upload-ready)
                                    ▼
                              ┌────────────┐
                              │ Uploading  │──┐
                              └─────┬──────┘  │  UploadChunkReceived (stay in Uploading; ACK)
                                    │◄───────┘
                                    │ UploadEndRequested
                                    ▼
                                   ┌─────────────────┐
                                   │ UploadFinalizing│──OpComplete──► Idle
                                   └─────────────────┘


  Idle ──DownloadRequested──► ┌──────────────┐
                               │ DownloadOpen │
                               └──────┬───────┘
                                      │ OpComplete (emit download-ready / first chunk)
                                      ▼
                               ┌──────────────┐
                               │ Downloading  │──┐
                               └──────┬───────┘  │  DownloadAckReceived (stay in Downloading; send next chunk)
                                      │◄────────┘
                                      │ OpComplete (EOF + final ACK)
                                      └──► Idle


  Any active state ──AbortRequested──► ┌───────────┐ ──OpComplete──► Idle
                                       │ Aborting  │
                                       └───────────┘

  Any active state ──SdRemoved──────────────────────────────────► Idle

  Any state ──StorageError──► Faulted  (or Idle for handled errors; see contract)
```

## Events

| Event | Meaning |
|---|---|
| `FileListRequested` | Desktop requested SD file list. |
| `FileLoadRequested` | Desktop requested loading/selecting a job file. |
| `FileUnloadRequested` | Desktop requested unloading the active job file. |
| `FileDeleteRequested` | Desktop requested deleting a file. |
| `UploadRequested` | Desktop requested a new upload session. |
| `UploadChunkReceived` | Binary upload data frame received. |
| `UploadEndRequested` | Desktop sent upload finalize/end command with expected CRC. |
| `DownloadRequested` | Desktop requested a download/preview session. |
| `DownloadAckReceived` | Binary download ACK frame received. |
| `AbortRequested` | Desktop, UI, disconnect, or error path requested transfer abort. |
| `SdRemoved` | SD card removed or mount became invalid during operation. |
| `StorageError` | FatFS, SD, protocol, or validation error occurred. |
| `OperationComplete` | Current operation completed successfully and FSM should return to `Idle`. |

## Errors

| Error | Meaning |
|---|---|
| `None` | No error. |
| `Busy` | Another operation is active. |
| `NotAllowed` | Machine state does not allow SD work. |
| `SdNotMounted` | SD card is absent or not mounted. |
| `FileNotFound` | Requested file does not exist. |
| `InvalidFilename` | Filename failed storage safety checks. |
| `InvalidSession` | Transfer frame/session ID does not match current session. |
| `BadSequence` | Chunk/ACK sequence was missing, duplicated unexpectedly, or out of order. |
| `SizeMismatch` | Uploaded/downloaded byte count does not match expected size. |
| `CrcMismatch` | Final CRC validation failed. |
| `ReadFail` | FatFS/SD read failed. |
| `WriteFail` | FatFS/SD write failed. |
| `NoSpace` | SD free space is insufficient for upload. |
| `Aborted` | Operation was intentionally aborted. |

## Storage Access Gate

All SD file operations should be accepted only when:

- `StorageState == Mounted`
- `MachineOperationState == Idle` or `MachineOperationState == TeensyDisconnected`
- `StorageTransferState == Idle`

This gate applies to:

- file list
- file load/unload
- file delete
- upload
- download/preview

If a request violates the gate, the transfer FSM should reject it with a structured error instead of partially starting the operation.

## Initial Transition Contract

The first implementation should keep FatFS synchronous and only move state ownership out of `DesktopProtocol`.

| Current State | Event | Next State | Notes |
|---|---|---|---|
| `Idle` | `FileListRequested` | `Listing` | Reject if gate fails. |
| `Idle` | `FileLoadRequested` | `Loading` | Reject if gate fails. |
| `Idle` | `FileUnloadRequested` | `Loading` | Treat unload as loaded-job metadata/storage operation. |
| `Idle` | `FileDeleteRequested` | `Deleting` | Reject if gate fails. |
| `Idle` | `UploadRequested` | `UploadOpen` | Validate name, size, free space, overwrite policy. |
| `UploadOpen` | `OperationComplete` | `Uploading` | File open succeeded, emit upload-ready. |
| `Uploading` | `UploadChunkReceived` | `Uploading` | Validate session/seq, write, ACK. Duplicate committed chunk re-ACKs without rewrite. |
| `Uploading` | `UploadEndRequested` | `UploadFinalizing` | Close file and validate size/CRC. |
| `UploadFinalizing` | `OperationComplete` | `Idle` | Emit upload complete and mark file list changed. |
| `Idle` | `DownloadRequested` | `DownloadOpen` | Validate name and open source file. |
| `DownloadOpen` | `OperationComplete` | `Downloading` | Emit download-ready and first data frame. |
| `Downloading` | `DownloadAckReceived` | `Downloading` | Send next data frame or complete at EOF. |
| `Downloading` | `OperationComplete` | `Idle` | Emit download complete. |
| Any active state | `AbortRequested` | `Aborting` | Close file, delete partial upload if needed, clear active session. |
| `Aborting` | `OperationComplete` | `Idle` | Emit abort acknowledgement. |
| Any active state | `SdRemoved` | `Idle` | Close/clear best-effort; emit SD/remove transfer error. |
| Any state | `StorageError` | `Faulted` or `Idle` | Fatal unrecovered errors go `Faulted`; handled per-operation errors return `Idle`. |

## Transfer Metadata Owned By FSM

The storage transfer FSM should own:

- current state
- operation type
- active filename
- transfer/session ID
- expected file size
- bytes written/sent
- expected sequence
- last ACKed sequence
- duplicate chunk/ACK counters
- retry counters
- running CRC
- final CRC
- FatFS `FIL`
- last error
- file-list-changed flag

`DesktopProtocol` should eventually become a thin adapter:

- parse text commands and binary transfer frames
- call storage FSM event handlers
- emit protocol responses produced by the FSM

## Binary File Transfer Transport

File payload traffic currently uses binary transfer frames:

| Frame Type | Direction | Meaning |
|---|---|---|
| `1` | Desktop -> Pico | Upload data chunk |
| `2` | Pico -> Desktop | Upload chunk ACK |
| `3` | Pico -> Desktop | Download data chunk |
| `4` | Desktop -> Pico | Download chunk ACK |

Session-level commands currently remain text (`@FILE_UPLOAD`, `@FILE_UPLOAD_END`, `@FILE_UPLOAD_ABORT`, `@FILE_DOWNLOAD`, `@FILE_DOWNLOAD_ABORT`, `@FILE_LIST`, `@FILE_LOAD`, `@FILE_UNLOAD`, `@FILE_DELETE`). Migration of these to binary frames is the next planned step.

The FSM must treat binary frame session IDs and sequence numbers as authoritative for active transfers.

## Non-Goals For This Step

~~Do not add these until the storage FSM owns current stop-and-wait behavior~~

The FSM refactor is complete. The items below remain future work but are no longer blocked:

- sliding window / pipelining
- cumulative ACKs
- resume after reconnect
- core-1 FatFS worker
- full desktop/Pico protocol migration to binary frames (next planned step)
