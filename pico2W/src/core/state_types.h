#pragma once

#include <cstdint>

enum class MachineState : uint8_t {
    Booting,
    Calibrating,
    Idle,
    Running,
    Hold,
    Alarm,
    Estop,
};

enum class JobState : uint8_t {
    NoFileSelected,
    FileSelected,
    Running,
};

enum class StorageState : uint8_t {
    Uninitialized,
    Mounting,
    Mounted,
    MountError,
    ScanError,
};
