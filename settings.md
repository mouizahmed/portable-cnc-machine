1. Define Settings Ownership *DONE*
      - Local desktop owns:
          - LastPort
          - AutoConnect
          - Units
          - ThemeMode
      - Pico owns:
          - StepsPerMmX/Y/Z
          - MaxTravelX/Y/Z
          - MaxFeedRateX/Y/Z
          - AccelerationX/Y/Z
          - SoftLimitsEnabled
          - HardLimitsEnabled
          - SpindleMinRpm
          - SpindleMaxRpm
          - WarningTemperature
          - MaxTemperature
      - Remove the visible concept of “local vs machine pending” from the UI. 
  2. Simplify Settings UI *DONE*
      - Settings page:
          - Always show APP.
          - Show MACHINE PROFILE, MOTION, LIMITS & SAFETY, and SPINDLE only when Pico is connected and
            machine settings have loaded.
      - Right rail:
          - Replace LOCAL and MACHINE badges with one badge:
              - SETTINGS
              - UNCHANGED or PENDING
          - Rename Save locally to Save.
          - Keep Revert.
          - Decide whether Load defaults should apply only to visible settings or all settings.
          - Remove:
              - Apply to machine
              - Secondary actions
              - Import
              - Export
  3. Add Machine Settings Protocol *DONE*
      - Add a protocol request for current Pico machine settings, for example:
          - desktop sends SETTINGS_GET
          - Pico replies with settings fields
      - Add a protocol command for saving machine settings, for example:
          - desktop sends SETTINGS_SET ...
          - Pico validates and replies SETTINGS_OK or SETTINGS_ERR reason
      - Use one clear snapshot format so desktop can populate the settings page directly from Pico.
  4. Load Settings On Connect *DONE*
      - When PiConnectionStatus becomes Connected:
          - request machine settings from Pico
          - populate machine fields from Pico response
          - mark machine snapshot as clean
          - show machine sections
      - If loading fails:
          - hide or disable machine sections
          - show a right rail status/error message
          - keep app/local preferences available
  5. Track One Pending State
      - Replace separate local/machine pending indicators with:
          - HasPendingSettingsChanges
      - This becomes true when any visible setting differs from its current clean snapshot.
      - Clean snapshot includes:
          - local app settings loaded from JSON
          - machine settings loaded from Pico, when connected
  6. Save Behavior
      - Save does both needed operations:
          - if app fields changed, write local JSON
          - if machine fields changed and connected, send settings to Pico
      - Only mark UNCHANGED after all required saves succeed.
      - If local save succeeds but Pico save fails:
          - keep state as PENDING
          - show error status
          - do not silently discard edits
  7. Revert Behavior
      - Revert restores values from the clean snapshot:
          - app fields from last local JSON load/save
          - machine fields from last Pico settings load/save
      - State returns to UNCHANGED.
  8. Defaults Behavior
      - Decide the product rule:
          - Option A: Load defaults affects only visible settings.
          - Option B: split into App defaults and Machine defaults.
      - I recommend Option A for now:
          - disconnected: defaults only app settings
          - connected: defaults app + visible machine settings
      - After loading defaults, state becomes PENDING until Save.
  9. Code Refactor
      - Split AppSettings into clearer models:
          - AppSettings
          - MachineSettings
      - SettingsService should persist only app settings, unless you explicitly want a non-
        authoritative machine cache.
      - SettingsViewModel should maintain:
          - current editable app values
          - clean app snapshot
          - current editable machine values
          - clean machine snapshot
          - machine settings load state
      - Remove:
          - MachineSettingsSyncPending
          - HasPendingMachineChanges
          - LocalPersistenceText
          - MachineSyncStatusText
          - ApplyMachineSettingsCommand
  10. Pico Firmware Work

  - Add machine settings storage/serialization on Pico if not already present.
  - On SETTINGS_GET, send current persisted settings.
  - On SETTINGS_SET, validate ranges before applying.
  - Persist settings only after validation succeeds.
  - Reply with explicit success/error.
