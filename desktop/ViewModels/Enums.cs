namespace PortableCncApp.ViewModels;

/// <summary>
/// Unified machine operation state as reported by the controller (STATE).
/// </summary>
public enum MachineOperationState
{
    Booting,
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
/// Safety supervision level as reported by the controller (SAFETY).
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
/// Desktop job lifecycle state for the loaded SD job.
/// </summary>
public enum JobRunState
{
    NoJob,
    Loaded,
    Running,
    Hold,
    Complete,
    Failed,
    Aborted
}

/// <summary>
/// Per-action capability flags as reported by the controller (CAPS).
/// The desktop binds these directly to UI controls — no local rule re-derivation.
/// </summary>
public record struct CapsFlags(
    bool Motion,
    bool Probe,
    bool Spindle,
    bool FileLoad,
    bool JobStart,
    bool JobPause,
    bool JobResume,
    bool JobAbort,
    bool Overrides,
    bool Reset);

/// <summary>Connection status for the desktop → Controller USB CDC link.</summary>
public enum ConnectionStatus
{
    Disconnected,
    Connecting,
    Connected,
    Error
}

public enum DesktopStorageState
{
    Idle,
    Refreshing,
    Uploading,
    DownloadingPreview,
    Deleting,
    Loading,
    Aborting,
    Failed
}
