#pragma once

#include <cstdint>

enum class MachineOperationState : uint8_t {
    Booting,
    Syncing,
    TeensyDisconnected,
    Idle,
    Homing,
    Jog,
    Starting,
    Running,
    Hold,
    Fault,
    Estop,
    CommsFault,
    Uploading,
};

enum class SafetyLevel : uint8_t {
    Safe,
    Monitoring,
    Warning,
    Critical,
};

struct CapsFlags {
    bool motion = false;
    bool probe = false;
    bool spindle = false;
    bool file_load = false;
    bool job_start = false;
    bool job_pause = false;
    bool job_resume = false;
    bool job_abort = false;
    bool overrides = false;
    bool reset = false;
};

enum class MachineEvent : uint8_t {
    TeensyConnected,
    TeensyDisconnected,
    GrblIdle,
    GrblHoming,
    GrblCycle,
    GrblHoldPending,
    GrblHoldComplete,
    GrblJog,
    GrblAlarm,
    GrblEstop,
    GrblDoor,
    GrblToolChange,
    GrblSleep,
    GcodeAck,
    GcodeError,

    HomeCmd,
    JogCmd,
    JogStop,
    StartCmd,
    PauseCmd,
    ResumeCmd,
    AbortCmd,
    ResetCmd,
    SpindleOnCmd,
    SpindleOffCmd,
    OverrideCmd,
    FileUploadCmd,
    UploadAbortCmd,

    HwEstopAsserted,
    HwEstopCleared,
    SdMounted,
    SdRemoved,
    JobLoaded,
    JobUnloaded,
    BootTimeout,
    SyncTimeout,
    JobStreamComplete,
    UploadComplete,
    UploadFailed,
    UploadAborted,
};

enum class StorageState : uint8_t {
    Uninitialized,
    Mounting,
    Mounted,
    MountError,
    ScanError,
};

enum class JobState : uint8_t {
    NoJobLoaded,
    JobLoaded,
    Running,
};
