# G-Code Validation Split

## Purpose

This project should treat G-code checking as a split responsibility across the desktop app, the Pico relay path, and the Teensy/GRBL controller.

The desktop app is responsible for preview, inspection, and lightweight preflight feedback.
The controller is responsible for authoritative command acceptance and runtime safety behavior.

## Responsibility Split

### Desktop App

The desktop app should handle:

- file loading and source preview
- toolpath preview generation
- lightweight parser warnings and source-level highlighting
- basic preflight errors when a file cannot be read or previewed
- user-facing messaging for preview limitations

The desktop app should not be treated as the final authority for:

- full GRBL syntax validation
- full GRBL modal/state validation
- guaranteed machine-run correctness
- safety certification of a job

### Pico

The Pico should be treated as the relay/supervisory layer.

It is responsible for:

- transport between desktop and Teensy
- supervisory I/O and status handoff
- local machine-side interface support

It is not the main G-code validator.

### Teensy / GRBL

The Teensy running the GRBL-compatible motion stack is the authoritative execution layer.

It is responsible for:

- accepting or rejecting commands
- enforcing controller syntax and modal/state rules
- feed hold, reset, alarms, limits, and runtime machine-state behavior
- surfacing execution errors back to the desktop

## Current Desktop Assessment

The current desktop implementation is good enough for the capstone split.

What it already does well:

- loads G-code files
- builds a preview toolpath
- shows parser warnings and file-read/parse errors
- gives the user enough inspection feedback before loading a job

What it does not do:

- full GRBL validation
- editor-grade diagnostics
- guaranteed pre-send verification of every line

That is acceptable for the capstone as long as the desktop side is described as:

- preview
- inspection
- lightweight preflight

and not as:

- full validator
- guaranteed execution verifier

## Recommendation

For the capstone, no major architectural changes are required on the desktop side for this split.

Recommended wording and behavior:

- describe desktop warnings as `preview warnings` or `preflight warnings`
- describe desktop failures as `preview/preflight errors`
- treat Teensy/GRBL errors as the final machine authority
- surface GRBL error/alarm responses clearly in the UI when command sending is wired

## Capstone Conclusion

The desktop implementation of the split is sufficient for the capstone.

Future improvements, if needed later, would be:

- a stronger GRBL-aware validator pass in the desktop app
- severity-tagged diagnostics (`preview`, `warning`, `blocking`)
- machine-profile checks for bounds and supported features
