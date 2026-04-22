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

### Job Intent Metadata

Queue priority alone is not enough. The coordinator also needs to know whether the worker
is doing real foreground work or disposable maintenance.

Every worker job should eventually carry metadata:

```cpp
enum class WorkerJobIntent : uint8_t {
    Urgent,
    ForegroundExclusive,
    ForegroundPreemptible,
    BackgroundDisposable,
};

struct WorkerJobPolicy {
    WorkerJobIntent intent;
    OperationSource source;
    bool preemptible;
    bool background;
    bool exclusive_sd;
};
```

Intent classes:

| Intent | Meaning | Examples |
|---|---|---|
| `Urgent` | Must run before all normal work. | abort transfer, cancel download/list, close active file after SD removal |
| `ForegroundExclusive` | User-visible operation that owns SD and should block other foreground SD work. | upload open/write/finalize, delete, explicit download, job-stream file ownership |
| `ForegroundPreemptible` | User-visible operation that can yield between bounded units. | file-list pages, preview/download chunks |
| `BackgroundDisposable` | Maintenance that can be skipped, delayed, coalesced, or discarded. | SD health check, free-space refresh, TFT/job-list cache refresh |

The worker snapshot should expose richer state than `worker_idle()`:

- `has_foreground_work`
- `has_exclusive_sd_work`
- `has_preemptible_foreground_work`
- `has_background_only_work`
- `urgent_pending`
- `active_intent`
- `active_operation`

The coordinator should use `worker_foreground_idle()` or equivalent instead of treating
queued health checks, free-space refreshes, and active upload writes as the same kind of
busy.

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
- Background telemetry/probe work must never block urgent or foreground motion commands.
- Foreground motion commands are mutually exclusive unless the target FSM explicitly
  defines a legal transition.
- Long-running foreground motion work must expose preemption points, but not all motion
  work is casually preemptible.

Motion intent classes:

| Intent | Examples | Policy |
|---|---|---|
| Safety / urgent | ESTOP, reset, feed hold, job abort, jog cancel, hard-limit/alarm handling | Preempts everything. |
| Foreground motion | job start, job streaming, homing, probing, jogging, zeroing | Mutually exclusive; controlled by machine/job/stream FSMs. |
| Background telemetry | position polling, caps/status polling, diagnostics, UI refresh | Droppable/coalescible; never blocks foreground or urgent work. |

Motion-specific constraints:

- Homing should only be interrupted by ESTOP, abort, or reset.
- Probing should only be interrupted by ESTOP, abort, or reset.
- Active job streaming should only be interrupted through hold, abort, ESTOP, or defined
  stream-cancel semantics.
- Jog should be cancellable, but a new jog should not replace an active jog without a
  clean cancel/idle boundary.

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

### Pico <-> Teensy Binary Framing Scope

This Core1Worker/concurrency refactor does not include converting the Pico <-> Teensy
UART protocol from ASCII lines to binary frames.

Reason:

- The immediate risk is scheduling and ownership, not wire encoding.
- The current ASCII protocol is easier to debug while refactoring concurrency.
- Changing transport framing at the same time would make failures harder to isolate.
- Core 0 UART ownership and request arbitration should be stable before changing the
  wire protocol.

Binary Pico <-> Teensy framing should be a later protocol migration with its own plan,
test matrix, and rollback point. That future migration can reuse the same core-0 UART
ownership rule: only one actor writes `uart1`, regardless of whether the bytes are ASCII
or binary.

## Request Arbitration

All incoming requests must pass through one central coordinator on core 0 before doing
work. This is mandatory, not advisory.

Target files:

- `pico2W/src/app/operations/operation_coordinator.h`
- `pico2W/src/app/operations/operation_coordinator.cpp`
- `pico2W/src/app/operations/operation_request.h`

The coordinator should become the only place that decides whether a desktop, TFT,
storage, stream, or motion request is accepted, queued, rejected, or preempted.

```cpp
RequestDecision OperationCoordinator::decide(const OperationRequest& request,
                                             const MachineFsm& machine,
                                             const StorageTransferStateMachine& storage,
                                             const JobStreamStateMachine& stream,
                                             const Core1WorkerSnapshot& worker);
```

Decision types:

- `AcceptNow`
- `Queue`
- `RejectBusy`
- `RejectInvalidState`
- `PreemptAndAccept`
- `AbortCurrentAndAccept`
- `SuppressBackgroundAndAccept`
- `CoalesceWithExisting`

This prevents desktop and TFT paths from applying different rules.

Implementation rule:

- `DesktopProtocol` may parse protocol frames, but it should not independently decide
  whether an operation is legal.
- TFT screens may create UI commands, but they should not independently decide whether an
  operation is legal.
- `PicoUartClient` may send authorized commands to Teensy, but it should not independently
  decide job-start legality.
- Storage classes may defensively validate state, but the coordinator is the canonical
  decision point.
- Scattered checks should be reduced to assertions, sanity validation, or final defense
  against misuse.
- Background maintenance must not produce user-visible busy responses unless it is already
  inside a non-preemptible hardware operation.
- Duplicate background/telemetry requests should be coalesced rather than stacked.
- Foreground requests may suppress pending background jobs before deciding whether the
  system is busy.

Operation request examples:

```cpp
enum class OperationRequestType : uint8_t {
    FileList,
    FileLoad,
    FileUnload,
    FileDelete,
    UploadBegin,
    UploadAbort,
    DownloadBegin,
    DownloadAbort,
    JobStart,
    JobHold,
    JobResume,
    JobAbort,
    Jog,
    JogCancel,
    HomeAll,
    ZeroAll,
    Estop,
    Reset,
    SettingsSave,
    CalibrationSave
};
```

The coordinator needs richer inputs than a single `worker_idle()` flag:

| Input | Meaning |
|---|---|
| `worker_foreground_idle` | No active or queued foreground storage work. Background jobs do not make this false. |
| `worker_background_only` | Worker has only health/free-space/cache work active or queued. |
| `worker_exclusive_sd_active` | Upload write/finalize, delete, explicit download, or stream file ownership is active. |
| `storage_transfer_active` | User-visible desktop/TFT storage transfer is active. |
| `stream_active` | Job stream state is not idle/complete. |
| `motion_command_in_flight` | A foreground motion command has been sent and is waiting on Teensy/GRBL state. |
| `telemetry_pending` | Background poll/probe/status work exists but can be dropped/coalesced. |
| `urgent_pending` | Safety or abort request pending. |

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
| File list page | Upload/delete/job start | `PREEMPT`/`QUEUE` | Finish current page at most, then cancel/defer list. |
| Background health/free-space/cache | Upload/delete/job start | `PREEMPT`/`OK` | Suppress or delay background work; do not surface busy. |
| Background telemetry/probe | Foreground motion command | `PREEMPT`/`OK` | Drop/coalesce telemetry; foreground command wins. |
| Delete file | Any SD request | `BUSY` | Delete is short but exclusive. |
| Job running | Upload/download/delete/load | `NO` | Keep SD free for active job/stream. |
| Job running | Pause/abort/estop | `PREEMPT` | Must bypass all queues. |
| Homing/probing | New motion command | `NO`/`BUSY` | Only ESTOP/abort/reset may interrupt. |
| Jog active | New jog | `BUSY` | Require cancel/idle boundary first. |
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

Status: Complete in firmware build.

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

Completed implementation notes:

- Added `pico2W/src/app/worker/core1_worker_types.h`.
- Added `pico2W/src/app/worker/core1_worker.h`.
- Added `pico2W/src/app/worker/core1_worker.cpp`.
- `Core1Worker` owns the explicit 8 KB core-1 stack, core-1 launch,
  `multicore_lockout_victim_init()`, urgent/control/bulk queues, result queue,
  heartbeat, busy state, active job state, and snapshots.
- `PortableCncApp` now owns `Core1Worker` as an app-level service.
- `DesktopProtocol` no longer owns core-1 launch, stack, worker lock, upload queue,
  upload result queue, or the core-1 entry loop.
- Existing upload chunk writes now submit `StorageWriteUploadChunk` bulk jobs and consume
  `UploadChunkCommitted` results through `Core1Worker`.
- Verified with `cmake --build build` in `pico2W`.

### Phase 1.5: OperationCoordinator

Status: Complete in firmware build.

- Create `OperationCoordinator` and `OperationRequest` files.
- Add one request enum covering desktop, TFT, storage, stream, and motion requests.
- Add one decision function using `MachineFsm`, `StorageTransferStateMachine`,
  `JobStreamStateMachine`, `Core1WorkerSnapshot`, SD mount state, and Teensy link state.
- Route desktop file requests through the coordinator.
- Route desktop motion/job requests through the coordinator.
- Route TFT file/job commands through the coordinator.
- Route job start/hold/resume/abort through the coordinator.
- Route settings and calibration save through the coordinator.
- Keep low-level guards as defensive validation only.
- Return consistent `BUSY`, `INVALID_STATE`, `NOT_ALLOWED`, and `PREEMPTED` outcomes.

Acceptance:

- No storage or motion operation starts from `DesktopProtocol` without a coordinator
  decision.
- No storage or motion operation starts from a TFT screen without a coordinator decision.
- File load while running is rejected through the coordinator.
- Upload/download/list/delete overlap is rejected through the coordinator.
- ESTOP, abort, and hold decisions preempt lower-priority storage/stream work.

Completed implementation notes:

- Added `pico2W/src/app/operations/operation_request.h`.
- Added `pico2W/src/app/operations/operation_coordinator.h`.
- Added `pico2W/src/app/operations/operation_coordinator.cpp`.
- `OperationCoordinator::decide()` now accepts `MachineFsm`,
  `StorageTransferStateMachine`, `JobStreamState`, `Core1WorkerSnapshot`, and
  `StorageState`.
- `JobStreamState` is present as a placeholder enum; current callers pass `Idle` until
  the real job-stream FSM exists.
- Desktop storage, motion/job, reset/estop, and settings-save handlers call the
  coordinator before starting work.
- TFT file/job/jog/home/zero commands are gated through the same coordinator.
- TFT settings save is gated through `PortableCncController`.
- Startup calibration save path checks `CalibrationSave` admission before entering the
  save-capable calibration workflow.
- Normal storage, motion, and flash-save work is rejected while storage transfer, worker,
  or stream work is active; estop and abort-style requests remain preemptive.
- Verified with `cmake --build build` in `pico2W`.

### Phase 2: Move Upload Worker

Status: Complete in firmware build; hardware upload and abort validation still required.

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

Completed implementation notes:

- Added `StorageOpenUpload`, `StorageFinalizeUpload`, and `StorageAbortTransfer` worker
  jobs alongside `StorageWriteUploadChunk`.
- Added `FileOpened`, `FileClosed`, and `TransferAborted` worker results.
- `Core1Worker` now owns the active upload `FIL`, transfer id, and filename.
- Upload open, existing-file check, overwrite unlink, free-space check, `f_open()`, and
  `f_expand()` now execute on core 1.
- Upload chunk writes continue to execute on core 1 through `StorageWriteUploadChunk`.
- Upload finalize now submits `StorageFinalizeUpload`; `f_sync()` and `f_close()` run on
  core 1, then core 0 emits the upload profile and completion response.
- Upload abort now submits `StorageAbortTransfer`; partial-file close/delete cleanup runs
  on core 1.
- `DesktopProtocol` applies upload worker results to `StorageTransferStateMachine` and
  remains responsible for protocol ACKs, errors, events, and UI state change flags.
- Verified with `cmake --build build` in `pico2W`.

### Phase 3: Move Download and Preview Reads

Status: Complete in firmware build; hardware download/preview and cancel validation still required.

- Add `StorageOpenDownload` and `StorageReadDownloadChunk`.
- Core 0 sends download frames from result data.
- Keep download ACK/session logic on core 0.

Acceptance:

- Desktop preview/download works.
- Cancel preview works immediately.
- Upload and download cannot overlap.

Completed implementation notes:

- Added `StorageOpenDownload`, `StorageReadDownloadChunk`, and `StorageCloseDownload`
  worker jobs.
- Added `DownloadOpened`, `DownloadChunkRead`, and `DownloadClosed` worker results.
- `Core1Worker` now owns the active download `FIL`, transfer id, and filename.
- Download stat/open now executes on core 1 and returns size/open status to core 0.
- Download reads now execute on core 1 and return bounded chunk payloads to core 0.
- Core 0 keeps the existing download frame format, ACK/session handling, retry timeout,
  CRC accumulation, and completion response behavior.
- Core 0 tracks pending read jobs separately from chunks already sent so the existing
  download ACK window remains bounded.
- Download abort/cancel clears pending worker jobs and closes the worker-owned download
  file through the worker path.
- Remaining direct FatFS calls in `DesktopProtocol` are file list/delete paths for Phase 4.
- Verified with `cmake --build build` in `pico2W`.

### Phase 4: Move File List, Delete, Free Space, Health

Status: Complete in firmware build; hardware file list/delete/SD-removal validation still required.

- Make file listing paged to avoid large result bursts.
- Move delete to worker.
- Move `f_getfree()` to worker.
- Move SD health sector reads to worker.

Acceptance:

- TFT and desktop remain responsive during file list.
- SD removal during file list or health check recovers cleanly.

Completed implementation notes:

- Added `StorageListBegin`, `StorageListNextPage`, `StorageDeleteFile`,
  `StorageFreeSpace`, and `StorageHealthCheck` worker jobs.
- Added `FileListPage`, `FileDeleted`, `FreeSpaceReady`, and `StorageHealthReady`
  worker results.
- File listing is internally paged at 8 entries per worker result.
- Desktop file-list behavior is preserved externally: core 0 still emits the existing
  `RESP_FILE_ENTRY` frames as entries arrive and the existing `RESP_FILE_LIST_END` when
  the worker reports completion.
- No desktop API pagination or TFT pagination UI was added.
- `Core1Worker` owns active directory scan state and closes the directory when listing
  completes or is aborted.
- Desktop delete now runs `f_unlink()` on core 1 and core 0 applies the result, unloads a
  deleted active job if needed, and emits the existing delete response.
- `f_getfree()` now runs on core 1 through list completion and `StorageFreeSpace`; core 0
  stores the latest free-space value in `StorageService`.
- `StorageService::free_bytes()` now returns cached worker-provided free-space data
  instead of calling FatFS directly.
- TFT/job-list refresh now consumes internally paged worker file-list results rather than
  calling `f_opendir()`/`f_readdir()` directly.
- Periodic SD health sector reads now run as `StorageHealthCheck` worker jobs; core 0
  applies the health result to `StorageService`.
- Desktop auto-connect file-list requests are now deferred inside `DesktopProtocol` if
  they race with worker startup/cache/health work, then started when the worker becomes
  available. This preserves the existing desktop file-list API and avoids surfacing
  transient `STORAGE_BUSY` during startup.
- Verified with `cmake --build build` in `pico2W`.

### Phase 4.5: Scheduler Policy and Intent Classification

Status: Partially complete in firmware build; motion telemetry coalescing and explicit
preemptible-list/download cancellation still required.

This phase removes the remaining "transient busy from maintenance work" cases by
classifying worker and motion work by intent instead of using one generic busy/idle signal.

Storage policy:

- Mark SD health checks as `BackgroundDisposable`.
- Mark free-space refresh as `BackgroundDisposable`.
- Mark TFT/job-list cache refresh as `BackgroundDisposable` or `ForegroundPreemptible`
  depending on whether the user explicitly requested it.
- Mark desktop/TFT explicit file listing as `ForegroundPreemptible`.
- Mark preview/download chunk reads as `ForegroundPreemptible` unless the user explicitly
  requested a blocking download.
- Mark upload open/write/finalize and delete as `ForegroundExclusive`.
- Let foreground commands suppress or clear pending background jobs.
- Let file list and preview/download yield between pages/chunks.
- Keep upload write/finalize/delete exclusive and non-preemptible except abort/removal.

Motion policy:

- Classify ESTOP, reset, feed hold, job abort, jog cancel, and alarm/limit handling as
  safety/urgent.
- Classify job start, job streaming, homing, probing, jogging, and zeroing as foreground
  motion.
- Classify position/status/caps polling, diagnostics, and UI refresh as background
  telemetry.
- Ensure urgent motion commands preempt all storage, stream, and telemetry work.
- Ensure foreground motion commands are mutually exclusive unless the relevant FSM
  explicitly allows the transition.
- Coalesce duplicate telemetry/background polls instead of stacking them.

Implementation tasks:

- Add worker job metadata: priority, source, intent, preemptible, background, exclusive SD.
- Add richer worker snapshot fields such as `has_foreground_work`,
  `has_background_only_work`, `has_exclusive_sd_work`, `active_intent`, and
  `urgent_pending`.
- Replace coordinator use of `worker_idle()` with `worker_foreground_idle()` or equivalent.
- Add operations to suppress or clear pending background worker jobs.
- Add coordinator rules for background motion telemetry.
- Add tests/logging for representative decisions:
  `UploadBegin + health check`, `JobStart + free-space refresh`, `Jog + telemetry pending`,
  `HomeAll + upload active`, and `Estop + anything active`.

Acceptance:

- Upload/delete/job-start do not return busy just because health/free-space/cache work is
  queued or active between bounded units.
- Explicit file list and preview/download can be cancelled or preempted at page/chunk
  boundaries.
- Background health/free-space/cache requests are skipped, delayed, or coalesced under
  foreground load.
- Background motion telemetry never blocks foreground motion or urgent commands.
- ESTOP/abort/hold still preempt every storage/stream/background path.

Completed implementation notes:

- Added `Core1JobIntent` and `Core1JobSource` metadata to worker jobs.
- Added richer `Core1WorkerSnapshot` fields: `has_foreground_work`,
  `has_background_only_work`, `has_exclusive_sd_work`,
  `has_preemptible_foreground_work`, `urgent_pending`, `active_intent`, and
  `active_operation`.
- `Core1Worker::snapshot()` now classifies active, queued, and pending-result work by
  intent instead of exposing only generic queue counts.
- `OperationCoordinator` now uses foreground-worker idleness for job start, storage
  requests, settings save, and calibration save. Background-only worker work returns
  `SuppressBackgroundAndAccept` instead of `RejectBusy`.
- Desktop, TFT, and startup calibration admission paths treat
  `SuppressBackgroundAndAccept` as an accepted decision.
- SD health checks, free-space refreshes, and `StorageService` job-list cache refreshes
  are marked `BackgroundDisposable`; explicit desktop file lists are marked
  `ForegroundPreemptible`.
- This slice implements storage-worker intent classification only. The motion-side policy
  is documented in this phase, but full motion telemetry/background coalescing still needs
  an equivalent motion scheduler or command-in-flight metadata layer before it can be
  enforced in code.
- Motion-side enforcement is deferred until Phase 6 introduces real stream/motion command
  state, or until a dedicated motion telemetry scheduler is added. Phase 4.5 defines the
  policy and implements the storage-worker half only.
- Verified with `cmake --build build` in `pico2W`.

### Phase 5: Refactor Flash Writer

Status: Complete in firmware build; calibration/settings/loaded-job persistence still needs
hardware validation after reflash.

- Introduce common reserved-flash write helper.
- Replace calibration, loaded-job, and machine-settings duplicated flash write code.
- Keep core-1 lockout centralized.

Acceptance:

- Calibration save, settings save, and loaded-job persistence all pass.
- No duplicated `flash_range_erase()` outside helper.

Completed implementation notes:

- Added `pico2W/src/app/flash/reserved_flash_writer.h`.
- Added `assert_reserved_flash_region()` so all reserved-flash readers/writers validate
  their flash offset remains beyond `__flash_binary_end`.
- Added `write_reserved_flash_sector()` to centralize:
  reserved-sector bounds checking, shared-SPI deselection, optional core-1 lockout,
  interrupt masking, flash erase/program, and raw sector verification.
- `CalibrationStorage`, `LoadedJobStorage`, and `MachineSettingsStore` now all use the
  shared helper instead of duplicating `flash_range_erase()` /
  `flash_range_program()` sequences.
- The final working design keeps the flash-write sequence shared but retains a local
  `alignas(FLASH_PAGE_SIZE)` sector buffer in each store. This preserves the proven
  per-store memory/layout pattern while still removing duplicated erase/program logic.
- `LoadedJobStorage` now benefits from the same shared-SPI deselection path used by the
  calibration/settings writers.
- Each store still performs its own semantic post-write verification after the helper's
  raw flash-sector verification.
- Verified with `cmake --build build` in `pico2W`.

### Phase 6: Job Stream Preparation

Status: Complete in firmware build; hardware UART streaming, hold/abort preemption, and
disconnect validation still required.

- Add job stream FSM.
- Add motion command/telemetry scheduler state used by `OperationCoordinator`, including
  command-in-flight, telemetry-pending, and urgent-pending inputs.
- Classify position/status/caps polling and diagnostics as background telemetry that can
  be dropped or coalesced under foreground motion.
- Core 0 authorizes job start.
- Core 1 opens selected SD file, counts valid G-code lines, and returns begin metadata.
- Core 1 prepares batches of trimmed G-code lines.
- Core 0 owns UART TX/RX and sends batches while respecting urgent motion commands.

Acceptance:

- Starting a job no longer blocks UI/USB while counting/streaming the file.
- Background motion telemetry never blocks foreground jog/home/zero/job-start requests.
- Duplicate telemetry polls are coalesced instead of stacked.
- Hold/abort/estop preempt streaming.
- Teensy disconnect cancels stream and moves to `CommsFault`.

Completed implementation notes:

- Added `pico2W/src/app/stream/job_stream_state_machine.h`.
- Added `pico2W/src/app/stream/job_stream_state_machine.cpp`.
- Added `pico2W/src/app/comm/motion_link_types.h`.
- Added `JobStreamPrepareBegin`, `JobStreamPrepareNextBatch`, and `JobStreamCancel`
  worker jobs.
- Added `StreamPrepareReady`, `StreamLineBatchReady`, and `StreamCancelled` worker
  results.
- `Core1Worker` now owns stream-file open/count/reseek/read/cancel work for SD-backed
  job streaming.
- `PicoUartClient::upload_and_run_loaded_job()` no longer reads the SD file
  synchronously on core 0. It now:
  submits a stream-prepare job, enters `Starting`/`Preparing`, sends `JOB_SELECT` and
  `JOB_BEGIN` when metadata is ready, streams worker-prepared line batches over UART on
  core 0, then sends `JOB_END` and `RUN`.
- `PicoUartClient` now tracks motion-link scheduler state via `MotionLinkSnapshot`,
  parses `@ACK` traffic, coalesces background telemetry probe work, and exposes stream
  state plus motion-link state to the coordinator.
- `OperationCoordinator` now gates job start and other foreground motion/storage
  requests using real job-stream state plus motion-link command-in-flight /
  urgent-pending inputs instead of a placeholder idle stream state.
- `DesktopProtocol` motion/job commands now route through `PicoUartClient` instead of
  the previous local stub transitions, and worker stream results are forwarded from the
  protocol worker-result drain into `PicoUartClient`.
- `PortableCncApp` and TFT start/jog/home flows now use the same worker-backed UART
  stream path as desktop commands.
- Verified with `cmake --build build` in `pico2W`.

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

### Phase 8: Optional Pico <-> Teensy Binary Protocol Migration

Do not do this as part of the Core1Worker/storage refactor. Only consider it after the
worker, coordinator, storage ownership, and job streaming phases are stable.

- Write a separate protocol plan for binary Pico <-> Teensy frames.
- Preserve core-0 ownership of `uart1` unless Phase 7 has intentionally changed UART TX
  ownership first.
- Define frame header, payload structs, CRC/checksum, resync behavior, and compatibility
  strategy.
- Migrate Teensy and Pico together behind a protocol version handshake.
- Keep ASCII diagnostics or a debug decode mode until binary framing is proven.

Acceptance:

- ASCII protocol can be restored by reverting only the protocol migration.
- Binary frames do not change `MachineFsm` or `OperationCoordinator` semantics.
- ESTOP, abort, hold, resume, and stream-line priority ordering is preserved.

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
- Should storage health checks be disabled during active streaming, or remain
  background-disposable jobs that run only when SD is idle?

## Immediate Next Step

Phase 1, Phase 1.5, Phase 2, Phase 3, and Phase 4 are complete in the firmware build.
Phase 4.5 is the next planned architecture cleanup before Phase 5.

Next:

1. Reflash and verify boot, calibration/settings save, and basic desktop/TFT command
   admission on hardware.
2. Retest `desktop/samples/test_2mb.gcode` upload, desktop upload abort, TFT upload abort,
   desktop download/preview, preview cancel, desktop/TFT file list, delete, SD removal
   during idle/list/health check, and post-transfer calibration/settings saves on hardware.
3. Implement Phase 4.5 scheduler policy and intent classification so background worker
   jobs and telemetry no longer produce generic busy decisions.
4. Continue Phase 5 by centralizing reserved-flash writes if Phase 2-4.5 hardware storage
   tests are stable.
