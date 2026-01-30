namespace PortableCncApp.ViewModels;

/// <summary>
/// Motion Controller (Teensy 4.1) FSM States
/// </summary>
public enum MotionState
{
    PowerUp,        // S0: System powering up
    Idle,           // S1: Ready, waiting for commands
    Homing,         // S2: Executing homing sequence
    Jog,            // S3: Manual jog in progress
    RunProgram,     // S4: Executing G-code program
    FeedHold,       // S5: Program paused (feed hold)
    Fault,          // S6: Fault condition (limit hit, driver fault)
    EStopLatched    // S7: Emergency stop activated
}

/// <summary>
/// Safety Supervisor (Raspberry Pi) FSM States
/// </summary>
public enum SafetyState
{
    SafeIdle,           // P0: System safe and idle
    Monitoring,         // P1: Actively monitoring during operation
    Warning,            // P2: Soft alarm (temp approaching limit, vibration high)
    EStopActive,        // P3: Emergency stop is active
    ShutdownSequence    // P4: Performing shutdown sequence
}

/// <summary>
/// Connection status to hardware
/// </summary>
public enum ConnectionStatus
{
    Disconnected,
    Connecting,
    Connected,
    Error
}
