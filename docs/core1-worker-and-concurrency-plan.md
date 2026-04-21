# Core 1 Worker and Concurrency Plan

## Purpose

The Pico currently owns UI, USB desktop protocol, SD/FatFS storage, flash persistence,
and the Pico <-> Teensy UART link from one main loop, with one core-1 helper for upload
SD writes. That worked for the first binary upload path, but it is not rigid enough for
future parallel work.

This plan defines the target architecture:

- Core 0 remains the owner of state, safety, UI, USB protocol dispatch, and request
  arbitration.
- Core 1 becomes a bounded worker executor with priority queues.
- Blocking I/O and long-running compute move behind explicit jobs.
- All shared hardware resources have one owner and one legal access path.
- Concurrent desktop, TFT, SD, and Teensy requests are accepted, queued, rejected, or
  preempted deterministically.

## Current Failure Lessons

The recent crashes exposed two hard rules:

- Once core 1 is running, flash erase/program must lock out core 1. Disabling interrupts
  on core 0 is not enough because core 1 may execute from XIP flash during erase.
- Core 1 needs explicit stack ownership for deep FatFS/SD paths. The SDK default core-1
  stack is too small for storage worker work.

The current patch set added:

- static app object allocation instead of placing the full app graph on `main()` stack
- explicit 8 KB core-1 worker stack
- core-1 multicore lockout registration
- multicore lockout around calibration, loaded-job, and machine-settings flash writes

These are necessary foundations, not the final worker architecture.

## Core Ownership Model

### Core 0 Owns

Core 0 is the coordinator and source of truth for observable state.

- `MachineFsm`
- `JobStateMachine`
- `JogStateMachine`
- `StorageTransferStateMachine`
- desktop USB protocol parsing and response emission
- TFT rendering and touch input
- high-level Pico <-> Teensy command authorization
- safety decisions and capability gating
- flash erase/program orchestration
- request admission, rejection, and preemption
- worker job submission and worker result application

Core 0 may read simple worker status snapshots, but must not share mutable state with
core 1 without a queue or lock.

### Core 1 Owns

Core 1 executes jobs that can block, take many milliseconds, or consume CPU.

Good core-1 jobs:

- SD upload chunk commit
- SD download chunk read
- SD file list scan
- SD delete/load metadata work when it can block
- SD free-space checks
- SD health checks
- G-code line counting
- G-code line read/trim/batch preparation for Teensy streaming
- optional future G-code preview preprocessing
- optional future CRC or metadata scan

Core 1 must not directly mutate Pico state machines. It reports results to core 0.

### Shared Resources

| Resource | Owner | Rule |
|---|---|---|
| `MachineFsm` | Core 0 | Core 1 never mutates it. |
| `JobStateMachine` | Core 0 | Core 1 reports file metadata; core 0 applies changes. |
| `StorageTransferStateMachine` | Core 0 | Core 1 reports operation progress/results. |
| TFT display | Core 0 | Core 1 never renders. |
| Touch controller | Core 0 | Core 1 never reads touch. |
| USB stdio / TinyUSB path | Core 0 | Core 1 never writes protocol frames directly. |
| Onboard flash | Core 0 | Core 0 locks out core 1, disables core-0 interrupts, writes flash, releases core 1. |
| SD/FatFS | Core 1 target owner | All FatFS calls should eventually go through one worker path. |
| Pico <-> Teensy UART TX | Core 0 | For this architecture phase, every `uart1` write stays on core 0. Core 1 prepares stream data only. |
| Pico <-> Teensy UART RX | Core 0 | For this architecture phase, every `uart1` read and Teensy state parse stays on core 0. |

## Core1Worker Target

Create a `Core1Worker` service to replace ad-hoc upload worker fields inside
`DesktopProtocol`.

Suggested files:

- `pico2W/src/app/worker/core1_worker.h`
- `pico2W/src/app/worker/core1_worker.cpp`
- `pico2W/src/app/worker/core1_worker_types.h`

The worker should be constructed during app startup, launch core 1 with an explicit stack,
register `multicore_lockout_victim_init()`, then process job queues forever.

### Public Interface

Core 0 submits jobs and polls results:

```cpp
class Core1Worker {
public:
    bool start();
    bool submit_urgent(const Core1Job& job);
    bool submit_control(const Core1Job& job);
    bool submit_bulk(const Core1Job& job);
    bool try_pop_result(Core1Result& result);
    Core1WorkerSnapshot snapshot() const;
    bool idle() const;
};
```

Avoid synchronous `wait_until_done()` calls from core 0 except during controlled shutdown
or boot-time diagnostics. Core 0 should remain responsive.

### Priority Queues

Yes: implement priority queues.

Use three queues at minimum:

| Queue | Purpose | Examples |
|---|---|---|
| `Urgent` | Must run before bulk work and should be checked between every chunk/line. | abort storage transfer, close active file, cancel stream, flush worker state |
| `Control` | Short bounded work needed to advance state. | open file, close file, delete file, list next directory page, get free space |
| `Bulk` | Throughput work that can be paused/preempted. | upload chunk write, download chunk read, G-code line batch prepare |

Processing rule:

1. Drain at most one urgent job.
2. Drain at most one control job.
3. Process one bounded bulk unit.
4. Repeat.

Bulk jobs must be chunked so urgent work can preempt quickly. A bulk job must not sit in
a long loop that prevents checking `Urgent`.

### Result Queue

Core 1 reports all completions to core 0:

```cpp
enum class Core1ResultType : uint8_t {
    UploadChunkCommitted,
    DownloadChunkRead,
    FileListEntry,
    FileListComplete,
    FileDeleted,
    FileOpened,
    FileClosed,
    FreeSpaceReady,
    StreamLineBatchReady,
    StreamComplete,
    StorageError,
    WorkerFault
};
```

Core 0 applies each result to the relevant FSM and emits protocol/UI updates.

## Job Types

### Storage Jobs

```cpp
enum class Core1JobType : uint8_t {
    StorageOpenUpload,
    StorageWriteUploadChunk,
    StorageFinalizeUpload,
    StorageOpenDownload,
    StorageReadDownloadChunk,
    StorageAbortTransfer,
    StorageListBegin,
    StorageListNextPage,
    StorageDeleteFile,
    StorageFreeSpace,
    StorageHealthCheck,
    JobStreamPrepareBegin,
    JobStreamPrepareNextBatch,
    JobStreamCancel
};
```

Do not pass pointers to stack memory. Job payloads should contain fixed-size structs or
references to worker-owned buffers.

### Buffer Ownership

Use worker-owned fixed buffers to avoid heap fragmentation and lifetime bugs:

- upload chunk slots
- download chunk slots
- file-list entry page
- G-code line batch buffer

Core 0 can copy incoming USB payloads into a free upload slot, submit the slot index, and
release the slot when the result is consumed.

## Storage/FatFS Ownership Migration

Current state:

- Upload SD writes are on core 1.
- File list, file delete, file load/unload, download reads, SD health checks, and Teensy
  job streaming still perform FatFS work on core 0 in places.

Target state:

- All FatFS operations go through `Core1Worker`.
- Core 0 never calls FatFS directly during normal runtime.
- Core 0 can still mount at boot initially, but even mount/remount should eventually be
  converted to worker jobs.

### Migration Order

1. Move existing upload queue out of `DesktopProtocol` into `Core1Worker`.
2. Move desktop download reads to `Core1Worker`.
3. Move file list and free-space scan to `Core1Worker`.
4. Move delete/load metadata and SD health checks to `Core1Worker`.
5. Move Pico -> Teensy job-file line preparation to `Core1Worker`.
6. Optional: move mount/remount and recovery paths to `Core1Worker`.

## Pico <-> Teensy Link Plan

The current UART protocol is line-oriented ASCII. That can remain for now, but request
ownership must be clearer.

Current blocker:

- `PicoUartClient::upload_and_run_loaded_job()` reads an SD file twice and streams all
  G-code lines synchronously from the main loop. This can block UI, USB handling, touch
  abort, and state updates.

Target behavior:

- Core 0 authorizes job start.
- Core 1 prepares stream batches from SD.
- Core 0 owns all UART TX/RX for this phase and sends prepared batches between polls.
- Urgent motion commands bypass or preempt streaming.

### Motion Command Priority

Motion-link commands need their own priority model:

| Priority | Commands |
|---|---|
| Safety critical | ESTOP, soft reset / abort |
| Realtime control | feed hold, resume, jog cancel |
| Normal control | home, zero, jog, job select, begin job, end job, run |
| Stream | G-code lines |
| Background | ping/hello/probe |

Rules:

- Safety critical and realtime control commands must never wait behind G-code stream
  lines.
- Stream sending must pause while hold/abort/estop is pending.
- Only core 0 writes `uart1` in this phase. If core 1 ever owns UART TX later, that must be a separate redesign where all TX moves together.
- UART RX state updates should continue to feed core 0 FSMs.

### Job Streaming State

Add a job-stream state machine or extend existing job state explicitly:

| State | Meaning |
|---|---|
| `Idle` | No stream active. |
| `Preparing` | Core 1 is counting/validating/opening the file. |
| `Beginning` | Core 0 sent job select/begin to Teensy. |
| `Streaming` | Lines are being sent. |
| `PausedByHold` | Stream paused due to hold/safety door/tool change. |
| `Cancelling` | Abort/reset requested; stream is being discarded. |
| `Complete` | All lines sent and job end sent. |
| `Faulted` | File read, UART, or Teensy error. |

Core 0 should map this into `MachineFsm` events, not replace `MachineFsm`.

## Request Arbitration

All incoming requests should pass through one admission function on core 0 before doing
work:

```cpp
RequestDecision RequestArbiter::decide(const Request& request,
                                       const MachineFsm& machine,
                                       const StorageTransferStateMachine& storage,
                                       const JobStreamStateMachine& stream,
                                       const WorkerSnapshot& worker);
```

Decision types:

- `AcceptNow`
- `Queue`
- `RejectBusy`
- `RejectInvalidState`
- `PreemptAndAccept`
- `AbortCurrentAndAccept`

This prevents desktop and TFT paths from applying different rules.

## Concurrency Matrix

Legend:

- `OK`: may run now.
- `BUSY`: reject or queue until current operation completes.
- `PREEMPT`: urgent request interrupts current work.
- `NO`: invalid state.

| Current Work | New Request | Decision | Notes |
|---|---|---|---|
| Upload | Upload/download/list/delete/load | `BUSY` | Single SD transfer at a time. |
| Upload | Upload abort | `PREEMPT` | Close file, delete partial upload if needed. |
| Upload | ESTOP / abort motion | `PREEMPT` | Motion safety bypasses storage busy. |
| Upload | Pause/resume job | `NO` | Upload should only be allowed idle/disconnected. |
| Download preview | Upload/list/delete/load | `BUSY` | Or cancel preview explicitly first. |
| Download preview | Preview cancel | `PREEMPT` | Stop sending chunks, close file. |
| File list | Upload/download/delete/load | `BUSY` | List should be short or paged. |
| Delete file | Any SD request | `BUSY` | Delete is short but exclusive. |
| Job running | Upload/download/delete/load | `NO` | Keep SD free for active job/stream. |
| Job running | Pause/abort/estop | `PREEMPT` | Must bypass all queues. |
| Job streaming | File list/upload/download/delete | `BUSY` | SD file is active for stream. |
| Job streaming | Hold/abort/estop | `PREEMPT` | Must pause/cancel stream immediately. |
| Job hold | Resume/abort/estop | `PREEMPT`/`OK` | Resume allowed when hold complete. |
| Job hold | File operations | `NO` | Keep state simple until job is idle/aborted. |
| Teensy disconnected | Upload/download/list/delete/load | `OK` if SD mounted | Storage may be used without motion link. |
| Teensy disconnected | Job start/home/jog | `NO` | Motion link unavailable. |
| Flash save | Any worker job | `BUSY`/lockout | Core 1 locked out during erase/program. |
| SD removed | Any active SD work | `PREEMPT` | Abort worker job and clear storage state. |

## Machine State Gates

Storage transfer requests should be accepted only when:

- `StorageState == Mounted`
- `StorageTransferState == Idle`
- machine state is `Idle` or `TeensyDisconnected`
- no job stream is active

Exceptions:

- transfer abort is always allowed when a transfer is active
- SD removed is always handled
- ESTOP / motion abort / hold must bypass storage busy

Job start should be accepted only when:

- machine state is `Idle`
- job loaded
- Teensy connected
- homed if homing is required
- no storage transfer active
- core-1 worker can reserve SD stream ownership

## Flash Write Policy

Every flash writer must use one shared helper eventually.

Target helper:

```cpp
bool write_reserved_flash_sector(uint32_t offset,
                                 const uint8_t* data,
                                 size_t length);
```

Required behavior:

1. Assert the target sector is reserved and after `__flash_binary_end`.
2. Wait until worker has no non-preemptible job active, or reject if not safe.
3. Lock out core 1 if the lockout victim is initialized.
4. Disable core-0 interrupts.
5. Erase/program flash.
6. Restore interrupts.
7. Release core 1.
8. Verify data.

Do not duplicate flash erase/program sequences in individual storage classes.

## Worker Health and Watchdog

Add a worker heartbeat:

- core 1 increments `heartbeat_counter` every loop
- core 0 records last observed heartbeat
- if heartbeat stops while worker should be alive, report `WorkerFault`

Add job timeout tracking:

- each job has a max expected duration
- long jobs must publish progress
- if timeout expires, core 0 transitions the relevant FSM to fault/abort path

For SD jobs, timeouts should be conservative because SD cards can stall, but they should
not be infinite.

## Error Handling

Worker results should include enough context:

- operation
- session id
- sequence
- filename
- FatFS `FRESULT`
- bytes requested
- bytes completed
- whether partial file should be deleted

Core 0 converts worker errors to:

- binary desktop protocol errors
- TFT status messages
- FSM events
- cleanup actions

Core 1 should not directly emit USB protocol responses.

## Implementation Phases

### Phase 1: Worker Skeleton

- Create `Core1Worker` files.
- Move core-1 launch and stack out of `DesktopProtocol`.
- Register `multicore_lockout_victim_init()` in worker entry.
- Add urgent/control/bulk queues.
- Add result queue.
- Add heartbeat/snapshot.
- Keep existing upload behavior until skeleton is stable.

Acceptance:

- Pico boots.
- Calibration save still works.
- Worker heartbeat visible in diagnostics/logs if enabled.

### Phase 2: Move Upload Worker

- Replace `DesktopProtocol` upload queue fields with `Core1Worker` upload jobs.
- Keep same upload frame format and ACK behavior.
- Core 0 receives upload data, validates session/sequence, copies into worker slot, submits
  `StorageWriteUploadChunk`.
- Core 1 writes chunk and returns `UploadChunkCommitted`.
- Core 0 updates `StorageTransferStateMachine` and sends ACK.

Acceptance:

- `desktop/samples/test_2mb.gcode` uploads reliably.
- Upload can be aborted from desktop.
- Upload can be aborted from TFT upload screen.
- Calibration/settings save still work before and after upload.

### Phase 3: Move Download and Preview Reads

- Add `StorageOpenDownload` and `StorageReadDownloadChunk`.
- Core 0 sends download frames from result data.
- Keep download ACK/session logic on core 0.

Acceptance:

- Desktop preview/download works.
- Cancel preview works immediately.
- Upload and download cannot overlap.

### Phase 4: Move File List, Delete, Free Space, Health

- Make file listing paged to avoid large result bursts.
- Move delete to worker.
- Move `f_getfree()` to worker.
- Move SD health sector reads to worker.

Acceptance:

- TFT and desktop remain responsive during file list.
- SD removal during file list or health check recovers cleanly.

### Phase 5: Refactor Flash Writer

- Introduce common reserved-flash write helper.
- Replace calibration, loaded-job, and machine-settings duplicated flash write code.
- Keep core-1 lockout centralized.

Acceptance:

- Calibration save, settings save, and loaded-job persistence all pass.
- No duplicated `flash_range_erase()` outside helper.

### Phase 6: Job Stream Preparation

- Add job stream FSM.
- Core 0 authorizes job start.
- Core 1 opens selected SD file, counts valid G-code lines, and returns begin metadata.
- Core 1 prepares batches of trimmed G-code lines.
- Core 0 owns UART TX/RX and sends batches while respecting urgent motion commands.

Acceptance:

- Starting a job no longer blocks UI/USB while counting/streaming the file.
- Hold/abort/estop preempt streaming.
- Teensy disconnect cancels stream and moves to `CommsFault`.

### Phase 7: Optional Future Motion Link Redesign

Do not do this as part of the Core1Worker/storage refactor. Only consider it later if
core-0 UART TX becomes a measured bottleneck after the SD/G-code preparation work has
moved to core 1.

- Move all UART TX ownership to one motion-link worker.
- Core 0 submits motion commands with priority.
- Core 1 serializes TX and returns ACK/error/status events.
- Core 0 still owns FSM mutation.

Acceptance:

- No code path writes directly to `uart1` except the motion-link worker. This applies
  only if the optional future motion-link redesign is intentionally started.
- Realtime commands preempt stream lines.

## Non-Goals

Do not do these as part of the first worker refactor:

- move UI rendering to core 1
- move state machines to core 1
- add heap-heavy dynamic job allocation
- let both cores write `uart1`
- let both cores call FatFS directly
- add resumable upload/download before the worker architecture is stable
- add binary Pico <-> Teensy protocol before current ASCII link is reliable

## Testing Plan

Run these after each phase:

1. Boot with no calibration stored, calibrate, save, reboot, verify calibration loads.
2. Save settings from desktop and reboot.
3. Upload `desktop/samples/test_2mb.gcode`.
4. Abort upload from desktop.
5. Abort upload from TFT.
6. Download/preview a file from SD.
7. Cancel preview while active.
8. List files while SD contains multiple G-code files.
9. Remove SD during idle.
10. Remove SD during upload/download.
11. Connect/disconnect Teensy while idle.
12. Start job, hold, resume, abort.
13. Start job and trigger Teensy disconnect.
14. Verify ESTOP path preempts any current storage/streaming work.

## Open Decisions

- Should core 1 eventually own all FatFS including mount/remount, or should mount remain a
  boot/core-0 exception?
- If core-0 UART TX ever becomes a measured bottleneck, should a separate motion-link
  redesign move all TX to a dedicated actor?
- How large should upload/download/stream buffers be given RAM pressure?
- Should file listing be fully paged over the desktop protocol?
- Should storage health checks be disabled during active streaming, or run as low-priority
  worker jobs only when SD is idle?

## Immediate Next Step

Implement Phase 1 and Phase 2 together only if the patch stays small. Otherwise:

1. Land `Core1Worker` skeleton with heartbeat and lockout registration.
2. Rebuild/reflash and verify calibration save.
3. Move upload writes from `DesktopProtocol` to `Core1Worker`.
4. Retest upload and abort paths before moving any other storage work.
