# Production Hardening Plan

## Goal

Keep the current architecture split:

- desktop app for preview, preflight, operator UX, and job management
- Pico for relay and supervisory I/O
- Teensy / GRBL-compatible control for authoritative execution and validation

This plan is for later work after the capstone baseline is complete.

## Guiding Principle

Do not move final G-code authority into the desktop app.

The desktop should become better at:

- catching obvious issues earlier
- presenting machine feedback clearly
- reducing operator confusion

The Teensy / GRBL layer should remain the final authority for:

- command acceptance
- modal/state legality
- alarms
- runtime stop/hold/reset behavior

## Phase 1: Desktop Preflight Improvements

Improve desktop-side feedback without pretending to be a full validator.

### Scope

- add preflight severity levels:
  - `Info`
  - `Warning`
  - `Blocking`
- distinguish:
  - preview-only issues
  - compatibility issues
  - machine-profile issues
- improve file-load and parse error messaging
- add clearer summaries in the files page and dashboard

### Suggested Checks

- unsupported or unknown commands for the chosen GRBL baseline
- malformed numeric tokens
- incomplete arc definitions
- suspicious coordinate/motion usage
- preview fallback conditions
- file bounds exceeding configured machine travel
- missing probe-related support if probing commands are present

### Deliverables

- `DiagnosticSeverity` model
- preflight result summary in the files page
- clear separation between `preview warnings` and `blocking preflight errors`

## Phase 2: Controller Error Propagation

Make controller responses visible and actionable in the desktop UI.

### Scope

- capture GRBL `error:` and `ALARM:` responses consistently
- map controller errors into user-friendly status text
- preserve raw controller response in diagnostics/console
- surface the last blocking machine error near the active job state

### Deliverables

- structured controller error model
- improved status banner / fault panel
- diagnostics log entries tied to command activity

## Phase 3: Run-State Hardening

Make runtime behavior safer and easier to reason about.

### Scope

- explicit job states for:
  - idle
  - running
  - feed hold
  - stopped
  - fault
  - disconnected
- clear transitions on:
  - pause
  - resume
  - stop
  - reset
  - disconnect
  - reconnect
- prevent conflicting UI actions during invalid states

### Deliverables

- audited state-transition table
- more reliable enable/disable logic for controls
- clearer recovery instructions after fault or disconnect

## Phase 4: Machine Profile Checks

Add lightweight physical-machine awareness on the desktop side.

### Scope

- define machine profile fields for:
  - axis travel
  - spindle RPM limits
  - probe availability
  - limit-switch availability
- compare loaded job bounds against travel limits
- warn when a file appears outside profile capability

### Deliverables

- machine profile model
- settings-backed profile configuration
- pre-run compatibility summary

## Phase 5: Transport and Streaming Reliability

Harden the Pico/Teensy communication path once command execution is fully wired.

### Scope

- reliable command send/ack tracking
- clear handling for:
  - timeouts
  - dropped connection
  - partial job interruption
  - fault during active run
- diagnostics logging around command streaming and responses

### Deliverables

- command queue / ack strategy
- timeout and retry policy
- resumable or explicitly non-resumable job-state rules

## Out of Scope for Near-Term Work

These are not required for the next phase and should not be prioritized yet:

- full editor-grade G-code validation
- stock simulation
- collision checking
- universal multi-dialect CNC verification
- industrial-grade autonomous recovery

## Recommended Order

1. Desktop preflight improvements
2. Controller error propagation
3. Run-state hardening
4. Machine profile checks
5. Transport and streaming reliability

## Capstone Positioning

For the capstone, the current split is acceptable.

This plan is for turning the same split into a stronger production-ready implementation later, without changing the overall architecture.
