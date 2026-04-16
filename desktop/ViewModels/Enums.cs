namespace PortableCncApp.ViewModels;

/// <summary>
/// Unified machine operation state as reported by the Pico (@STATE).
/// Mirrors the 13-state MachineStateMachine defined in STATE_MACHINE.md.
/// </summary>
public enum MachineOperationState
{
    Booting,
    TeensyDisconnected,
    Syncing,
    Idle,
    Homing,
    Jog,
    Starting,
    Running,
    Hold,
    Fault,
    Estop,
    CommsFault,
    Uploading   // SD card file upload in progress — all action caps suppressed
}

/// <summary>
/// Safety supervision level as reported by the Pico (@SAFETY).
/// Orthogonal to operation state.
/// </summary>
public enum SafetyLevel
{
    Safe,
    Monitoring,
    Warning,
    Critical
}

/// <summary>
/// Per-action capability flags as reported by the Pico (@CAPS).
/// The desktop binds these directly to UI controls — no local rule re-derivation.
/// </summary>
public record struct CapsFlags(
    bool Motion,
    bool Probe,
    bool Spindle,
    bool FileSelect,
    bool JobStart,
    bool JobPause,
    bool JobResume,
    bool JobAbort,
    bool Overrides,
    bool Reset);

/// <summary>Connection status for the desktop → Pico USB CDC link.</summary>
public enum ConnectionStatus
{
    Disconnected,
    Connecting,
    Connected,
    Error
}

// ── Temporary compatibility stubs — remove when MainWindowViewModel is reworked ──

[Obsolete("Replace with MachineOperationState. Remove after Phase 2 MainWindowViewModel rework.")]
public enum MotionState
{
    PowerUp, Idle, Homing, Jog, RunProgram, FeedHold, Fault, EStopLatched
}

[Obsolete("Replace with SafetyLevel. Remove after Phase 2 MainWindowViewModel rework.")]
public enum SafetyState
{
    SafeIdle, Monitoring, Warning, EStopActive, ShutdownSequence
}
