• Proposed Plan


  # Pico Runtime Reimplementation Plan: Multicore Scheduler and Blocking Elimination

  ## Summary

  Rebuild the Pico firmware runtime around a strict two-core architecture with explicit ownership, message passing, and
  bounded work. The goal is to eliminate all known and likely blocking hazards, not just SD stalls, while keeping the
  Pico as sole state owner and allowing protocol cleanup where it materially improves correctness.

  Target architecture:

  - Core 0: control plane and latency-critical loop
  - Core 1: blocking/background work executor
  - No direct long-running IO or scanning on core 0
  - No shared mutable subsystem ownership across cores
  - All cross-core interaction through typed request/result queues

  This is a ground-up runtime refactor, not a local patch.

  ## Runtime Architecture

  ### Core ownership

  - Core 0 owns:
      - USB CDC transport
      - desktop protocol parsing and protocol emission
      - machine FSM, job FSM, jog FSM
      - startup state orchestration
      - touch input polling
      - UI navigation and render scheduling
      - final ownership of all externally visible state
  - Core 1 owns:
      - SD card driver usage
      - FatFS calls
      - storage mount/remount/health checks
      - directory scans
      - file upload writes
      - file download reads
      - future heavy/background IO tasks

  ### Cross-core model

  - Add two typed queues:
      - Core0 -> Core1 work queue
      - Core1 -> Core0 result/event queue
  - Queue payloads must be fixed-size structs with enum-tagged operations.
  - Core 0 never calls FatFS or SD driver directly after the refactor.
  - Core 1 never mutates machine/job/jog FSMs directly.
  - Core 1 reports outcomes; core 0 applies state transitions and emits protocol/UI updates.

  ### Scheduling rules

  - Core 0 loop order:
      1. poll USB/protocol input
      2. drain core 1 result queue
      3. process machine/UI events
      4. schedule small render work
      5. enqueue background work if needed
      6. sleep/yield briefly
  - Core 1 loop order:
      1. drain work queue
      2. execute one bounded task or one task slice
      3. emit result/progress event
      4. yield
  - Long operations must be sliced where possible:
      - file list scan by entry batches
      - downloads by chunk
      - uploads by chunk
      - health checks by single check interval
  - If an operation cannot be sliced cleanly, it still stays on core 1 so core 0 remains responsive.

  ### Startup and calibration

  - Include startup in the new scheduler model.
  - Boot sequence becomes explicit phases on core 0:
      - BOOT_INIT
      - CALIBRATION_REQUIRED or CALIBRATION_READY
      - STORAGE_INIT_PENDING
      - STORAGE_READY or STORAGE_ERROR
      - TEENSY_SYNC_PENDING
      - RUNNING_UI
  - Storage init/remount requests are dispatched to core 1, not executed inline.
  - Touch calibration may remain modal from a UX perspective, but must not require storage work or protocol polling to
    block core 0.
  - During calibration mode, protocol remains alive and returns a busy/status state rather than appearing dead.

  ### Rendering and UI

  - Keep display/touch/UI on core 0.
  - Rendering remains synchronous initially, but only in response to state deltas.
  - Do not perform full-screen redraws from deep subsystem calls.
  - Replace “do work and render immediately” patterns with:
      - subsystem emits state/result
      - core 0 marks dirty regions or full refresh flag
      - renderer applies updates in the normal loop
  - File screen refresh must become request-based:
      - TFT refresh button enqueues storage refresh work on core 1
      - core 0 updates UI when results arrive

  ## Protocol, State, and Interface Changes

  ### State model

  - Keep the Pico as sole state owner.
  - Keep the unified machine FSM, but remove any runtime paths that directly tie blocking storage execution to FSM
    processing.
  - Retire the old unused machine_state_machine.* implementation entirely as dead code during the refactor.
  - Storage becomes an explicit asynchronous subsystem with its own operational status:
      - UNINITIALIZED
      - MOUNT_PENDING
      - MOUNTED
      - MOUNT_ERROR
      - SCAN_PENDING
      - SCAN_ERROR
      - HEALTH_DEGRADED
  - Machine operation state and storage state must no longer be conflated.

  ### Protocol cleanup

  Because protocol cleanup is allowed, make these contract changes:

  - Preserve existing high-value verbs where possible:
      - @PING, @INFO, @STATUS
      - file load/unload/download/upload verbs
      - @STATE, @CAPS, @SAFETY, @JOB, @EVENT
  - Expand @STATUS into a true structured snapshot instead of relying on follow-up inference:
      - include machine state, storage state, controller link state, loaded job, and busy subsystem state
  - Add explicit storage progress/busy events:
      - @EVENT STORAGE_BUSY PHASE=<mount|scan|health|upload|download>
      - @EVENT STORAGE_READY
      - @EVENT STORAGE_ERROR REASON=<...>
  - Add explicit “controller alive but subsystem busy” semantics:
      - use @WAIT REASON=<...> only for command throttling
      - never let storage trouble imply controller transport loss
  - Keep preview selection out of protocol.
  - Keep loaded job as protocol-visible state only.
  - Keep job persistence by filename.

  ### Internal interfaces

  Create explicit background service interfaces:

  - StorageWorkerRequest
      - Mount
      - Remount
      - HealthCheck
      - ScanFiles
      - DeleteFile
      - BeginUpload
      - WriteUploadChunk
      - FinalizeUpload
      - AbortUpload
      - BeginDownload
      - ReadDownloadChunk
      - AbortDownload
  - StorageWorkerResult
      - success/failure plus reason
      - optional file metadata, free-space, chunk payload, progress counters
  - UiRenderRequest
      - dirty region or full refresh flag
  - ProtocolOutboundEvent
      - only core 0 sends to USB, but use explicit internal event structs so protocol emission is centralized

  ### Behavioral requirements

  - SD failures must surface as storage errors, not transport disconnects.
  - USB protocol must remain responsive while storage is mounting, failing, scanning, uploading, or downloading.
  - TFT actions must remain responsive even if storage is unhealthy.
  - Desktop-visible disconnect means actual controller-link loss, not a blocked storage operation.
  - Upload/download must remain resumeless for now; keep current all-or-nothing completion semantics.

  ## Implementation Plan

  ### Phase 1: Runtime skeleton

  - Add multicore bootstrap in main.cpp.
  - Introduce core-safe queues, event structs, and a runtime coordinator.
  - Move SD/FatFS ownership to a dedicated worker service on core 1.
  - Keep core 0 protocol alive with current handshake/status behavior from the first commit of the refactor.

  ### Phase 2: Storage async conversion

  - Refactor StorageService into a core-1 worker-facing service.
  - Remove all direct storage polling/remount work from the main UI loop.
  - Convert file list refresh and storage health checks into queued work.
  - Ensure mount/scan/health transitions emit result events instead of mutating UI or FSM state inline.

  ### Phase 3: Protocol and transfer refactor

  - Refactor DesktopProtocol so command handlers only:
      - validate
      - enqueue background work
      - update FSM if appropriate
      - emit immediate accept/busy/error responses
  - Move upload/download file IO fully behind worker messages.
  - Keep chunk protocol bounded and non-blocking from core 0’s point of view.
  - Centralize protocol output on core 0 only.

  ### Phase 4: UI/event cleanup

  - Remove direct render calls from subsystem actions.
  - Make TFT screens consume state snapshots and dirty flags only.
  - Convert file refresh/load flows to async result-driven updates.
  - Ensure startup and calibration integrate with the new runtime phases.

  ### Phase 5: Cleanup and docs

  - Remove dead single-core assumptions and obsolete state-machine code.
  - Update PROTOCOL.md and STATE_MACHINE.md to reflect:
      - multicore ownership
      - async storage behavior
      - explicit storage state
      - controller-alive vs subsystem-busy distinction

  ## Test Plan

  ### Runtime responsiveness

  - Desktop remains connected during:
      - SD mount success
      - SD mount failure
      - repeated remount retries
      - scan error
      - large file list scan
      - upload
      - download
  - @PING and @STATUS remain responsive during all storage phases.

  ### Correctness

  - Loaded job survives remount if file still exists.
  - Loaded job clears correctly on SD removal.
  - Preview/download never changes loaded job.
  - Upload/download/delete/list results stay in sync across TFT and desktop.
  - Machine FSM transitions remain correct during:
      - boot
      - sync
      - idle
      - homing
      - jog
      - running
      - hold
      - fault
      - uploading

  ### Stress and race cases

  - Repeated desktop @STATUS during remount.
  - Repeated TFT refresh taps during scan/mount error.
  - SD remove/insert during upload.
  - SD remove/insert during download.
  - Desktop reconnect during storage busy.
  - Simultaneous TFT and desktop file actions; verify queue serialization and deterministic result ordering.

  ## Assumptions and Defaults

  - Chosen architecture: strict split, core 0 for protocol/UI/FSM, core 1 for storage/background work.
  - Startup is included in the redesign, not left as a blocking exception.
  - Protocol cleanup is allowed if it improves runtime correctness.
  - Desktop protocol remains the primary external contract; web UI remains future work.
  - No attempt will be made in this refactor to add transfer resume semantics.
  - Rendering stays on core 0 for now; this refactor fixes scheduling first, not GPU/display throughput.