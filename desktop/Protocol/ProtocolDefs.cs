using System;
using System.Runtime.InteropServices;

namespace PortableCncApp.Protocol;

public static class ProtocolConstants
{
    public const byte ProtocolVersion = 1;
    public const byte TransferIdNone = 0;
    public const int FrameHeaderSize = 9;

    public const int MaxFilenameBytes = 64;
    public const int MaxFirmwareBytes = 16;
    public const int MaxBoardBytes = 16;
    public const int MaxReasonBytes = 64;
    public const int MaxMessageBytes = 64;
}

public enum FrameType : byte
{
    UploadData = 1,
    UploadAck = 2,
    DownloadData = 3,
    DownloadAck = 4,
    Cmd = 5,
    Resp = 6,
    Event = 7
}

public enum CommandMessageType : byte
{
    Ping = 1,
    Info = 2,
    Status = 3,
    Home = 4,
    ProbeZ = 5,
    Jog = 6,
    JogCancel = 7,
    Zero = 8,
    Start = 9,
    Pause = 10,
    Resume = 11,
    Abort = 12,
    Estop = 13,
    Reset = 14,
    SpindleOn = 15,
    SpindleOff = 16,
    Override = 17,
    FileList = 18,
    FileLoad = 19,
    FileUnload = 20,
    FileDelete = 21,
    FileUpload = 22,
    FileUploadEnd = 23,
    FileUploadAbort = 24,
    FileDownload = 25,
    FileDownloadAck = 26,
    FileDownloadAbort = 27,
    BeginJob = 28,
    EndJob = 29,
    ClearJob = 30,
    SettingsGet = 31,
    SettingsSet = 32
}

public enum ResponseMessageType : byte
{
    Pong = 1,
    Info = 2,
    State = 3,
    Caps = 4,
    Safety = 5,
    Job = 6,
    Pos = 7,
    CommandAck = 8,
    Error = 9,
    FileEntry = 10,
    FileListEnd = 11,
    FileLoad = 12,
    FileUnload = 13,
    FileDelete = 14,
    FileUploadReady = 15,
    FileUploadEnd = 16,
    FileUploadAbort = 17,
    FileDownloadReady = 18,
    FileDownloadEnd = 19,
    FileDownloadAbort = 20,
    StorageError = 21,
    Wait = 22,
    MachineSettings = 23
}

public enum EventMessageType : byte
{
    State = 1,
    Caps = 2,
    Safety = 3,
    Job = 4,
    Pos = 5,
    StateChanged = 6,
    JobProgress = 7,
    JobComplete = 8,
    JobError = 9,
    SdMounted = 10,
    SdRemoved = 11,
    TeensyConnected = 12,
    TeensyDisconnected = 13,
    EstopActive = 14,
    EstopCleared = 15,
    Limit = 16,
    StorageUploadProfile = 17,
    StorageUploadChunkProfile = 18,
    Alarm = 19
}

public enum AxisId : byte
{
    X = 0,
    Y = 1,
    Z = 2
}

[Flags]
public enum AxesMask : byte
{
    None = 0,
    X = 1 << 0,
    Y = 1 << 1,
    Z = 1 << 2,
    All = X | Y | Z
}

public enum OverrideTarget : byte
{
    Feed = 0,
    Spindle = 1,
    Rapid = 2
}

public enum ProtocolMachineState : byte
{
    Booting = 0,
    Syncing = 1,
    TeensyDisconnected = 2,
    Idle = 3,
    Homing = 4,
    Jog = 5,
    Starting = 6,
    Running = 7,
    Hold = 8,
    Fault = 9,
    Estop = 10,
    CommsFault = 11,
    Uploading = 12
}

public enum ProtocolSafetyLevel : byte
{
    Safe = 0,
    Monitoring = 1,
    Warning = 2,
    Critical = 3
}

[Flags]
public enum CapabilityFlags : ushort
{
    Motion = 1 << 0,
    Probe = 1 << 1,
    Spindle = 1 << 2,
    FileLoad = 1 << 3,
    JobStart = 1 << 4,
    JobPause = 1 << 5,
    JobResume = 1 << 6,
    JobAbort = 1 << 7,
    Overrides = 1 << 8,
    Reset = 1 << 9
}

public enum ProtocolStorageOperation : byte
{
    None = 0,
    List = 1,
    Load = 2,
    Unload = 3,
    Delete = 4,
    Upload = 5,
    Download = 6
}

public enum ProtocolErrorCode : byte
{
    None = 0,
    InvalidState = 1,
    MissingParam = 2,
    NoJobLoaded = 3,
    UploadFileExists = 4,
    StorageBusy = 5,
    StorageNotAllowed = 6,
    StorageNoSd = 7,
    StorageFileNotFound = 8,
    StorageInvalidFilename = 9,
    StorageInvalidSession = 10,
    StorageBadSequence = 11,
    StorageSizeMismatch = 12,
    StorageCrcFail = 13,
    StorageReadFail = 14,
    StorageWriteFail = 15,
    StorageNoSpace = 16,
    StorageAborted = 17,
    DownloadMissingParam = 18,
    UploadMissingParam = 19,
    Unknown = 255
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct ProtocolFrameHeader
{
    public byte Type;
    public byte TransferId;
    public byte Flags;
    public uint Seq;
    public ushort PayloadLen;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdPing { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdInfo { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdStatus { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdHome { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdProbeZ { public byte MessageType; public float Depth; public ushort Feed; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdJog { public byte MessageType; public byte Axis; public float Dist; public ushort Feed; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdJogCancel { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdZero { public byte MessageType; public byte AxesMask; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdStart { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdPause { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdResume { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdAbort { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdEstop { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdReset { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdSpindleOn { public byte MessageType; public ushort Rpm; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdSpindleOff { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdOverride { public byte MessageType; public byte Target; public ushort Percent; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdFileList { public byte MessageType; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct CmdFileLoad
{
    public byte MessageType;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdFileUnload { public byte MessageType; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct CmdFileDelete
{
    public byte MessageType;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct CmdFileUpload
{
    public byte MessageType;
    public uint Size;
    public byte Overwrite;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdFileUploadEnd { public byte MessageType; public uint Crc32; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdFileUploadAbort { public byte MessageType; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct CmdFileDownload
{
    public byte MessageType;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdFileDownloadAck { public byte MessageType; public byte TransferId; public uint Seq; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdFileDownloadAbort { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdBeginJob { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdEndJob { public byte MessageType; public uint Lines; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdClearJob { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct CmdSettingsGet { public byte MessageType; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct CmdSettingsSet
{
    public byte MessageType;
    public float StepsPerMmX;
    public float StepsPerMmY;
    public float StepsPerMmZ;
    public float MaxFeedRateX;
    public float MaxFeedRateY;
    public float MaxFeedRateZ;
    public float AccelerationX;
    public float AccelerationY;
    public float AccelerationZ;
    public float MaxTravelX;
    public float MaxTravelY;
    public float MaxTravelZ;
    public byte SoftLimitsEnabled;
    public byte HardLimitsEnabled;
    public float SpindleMinRpm;
    public float SpindleMaxRpm;
    public float WarningTemperature;
    public float MaxTemperature;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespPong { public byte MessageType; public uint RequestSeq; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespInfo
{
    public byte MessageType;
    public uint RequestSeq;
    public fixed byte Firmware[ProtocolConstants.MaxFirmwareBytes];
    public fixed byte Board[ProtocolConstants.MaxBoardBytes];
    public byte TeensyConnected;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespState { public byte MessageType; public uint RequestSeq; public byte State; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespCaps { public byte MessageType; public uint RequestSeq; public ushort Caps; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespSafety { public byte MessageType; public uint RequestSeq; public byte Safety; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespJob
{
    public byte MessageType;
    public uint RequestSeq;
    public byte HasJob;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct RespPos
{
    public byte MessageType;
    public uint RequestSeq;
    public float Mx;
    public float My;
    public float Mz;
    public float Wx;
    public float Wy;
    public float Wz;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespCommandAck { public byte MessageType; public uint RequestSeq; public byte CommandType; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespError
{
    public byte MessageType;
    public uint RequestSeq;
    public byte Error;
    public fixed byte Reason[ProtocolConstants.MaxReasonBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileEntry
{
    public byte MessageType;
    public uint RequestSeq;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
    public uint Size;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespFileListEnd { public byte MessageType; public uint RequestSeq; public uint Count; public ulong FreeBytes; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileLoad
{
    public byte MessageType;
    public uint RequestSeq;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespFileUnload { public byte MessageType; public uint RequestSeq; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileDelete
{
    public byte MessageType;
    public uint RequestSeq;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileUploadReady
{
    public byte MessageType;
    public uint RequestSeq;
    public byte TransferId;
    public uint Size;
    public ushort ChunkSize;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileUploadEnd
{
    public byte MessageType;
    public uint RequestSeq;
    public byte TransferId;
    public uint Size;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespFileUploadAbort { public byte MessageType; public uint RequestSeq; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileDownloadReady
{
    public byte MessageType;
    public uint RequestSeq;
    public byte TransferId;
    public uint Size;
    public ushort ChunkSize;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespFileDownloadEnd
{
    public byte MessageType;
    public uint RequestSeq;
    public byte TransferId;
    public uint Crc32;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct RespFileDownloadAbort { public byte MessageType; public uint RequestSeq; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespStorageError
{
    public byte MessageType;
    public uint RequestSeq;
    public byte Error;
    public byte Operation;
    public uint Seq;
    public uint Expected;
    public uint Actual;
    public fixed byte Detail[ProtocolConstants.MaxReasonBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct RespWait
{
    public byte MessageType;
    public uint RequestSeq;
    public fixed byte Reason[ProtocolConstants.MaxReasonBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct RespMachineSettings
{
    public byte MessageType;
    public uint RequestSeq;
    public float StepsPerMmX;
    public float StepsPerMmY;
    public float StepsPerMmZ;
    public float MaxFeedRateX;
    public float MaxFeedRateY;
    public float MaxFeedRateZ;
    public float AccelerationX;
    public float AccelerationY;
    public float AccelerationZ;
    public float MaxTravelX;
    public float MaxTravelY;
    public float MaxTravelZ;
    public byte SoftLimitsEnabled;
    public byte HardLimitsEnabled;
    public float SpindleMinRpm;
    public float SpindleMaxRpm;
    public float WarningTemperature;
    public float MaxTemperature;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventState { public byte MessageType; public byte State; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventCaps { public byte MessageType; public ushort Caps; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventSafety { public byte MessageType; public byte Safety; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct EventJob
{
    public byte MessageType;
    public byte HasJob;
    public fixed byte Name[ProtocolConstants.MaxFilenameBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct EventPos
{
    public byte MessageType;
    public float Mx;
    public float My;
    public float Mz;
    public float Wx;
    public float Wy;
    public float Wz;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventStateChanged { public byte MessageType; public byte State; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventJobProgress { public byte MessageType; public uint Line; public uint Total; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventJobComplete { public byte MessageType; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct EventJobError
{
    public byte MessageType;
    public fixed byte Reason[ProtocolConstants.MaxReasonBytes];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventSdMounted { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventSdRemoved { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventTeensyConnected { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventTeensyDisconnected { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventEstopActive { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventEstopCleared { public byte MessageType; }
[StructLayout(LayoutKind.Sequential, Pack = 1)] public struct EventLimit { public byte MessageType; public byte AxesMask; }

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct EventStorageUploadProfile
{
    public byte MessageType;
    public uint Size;
    public uint TotalMs;
    public uint PreallocMs;
    public uint WriteMs;
    public uint MaxWriteMs;
    public uint CloseMs;
    public uint Chunks;
    public uint QueueMax;
    public uint Bps;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct EventStorageUploadChunkProfile
{
    public byte MessageType;
    public uint Seq;
    public uint Bytes;
    public uint TotalMs;
    public uint WriteMs;
    public uint LastWriteMs;
    public uint MaxWriteMs;
    public uint Chunks;
    public uint Queue;
    public uint QueueMax;
    public uint Bps;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct EventAlarm
{
    public byte MessageType;
    public ushort Code;
    public fixed byte Message[ProtocolConstants.MaxMessageBytes];
}
