#pragma once

#include <stdint.h>

#ifdef __cplusplus
#define PCNC_STATIC_ASSERT static_assert
#else
#define PCNC_STATIC_ASSERT _Static_assert
#endif

#define PCNC_PROTOCOL_VERSION 1u
#define PCNC_TRANSFER_ID_NONE 0u
#define PCNC_FRAME_HEADER_SIZE 9u

#define PCNC_MAX_FILENAME_BYTES 64u
#define PCNC_MAX_FIRMWARE_BYTES 16u
#define PCNC_MAX_BOARD_BYTES 16u
#define PCNC_MAX_REASON_BYTES 64u
#define PCNC_MAX_MESSAGE_BYTES 64u

#define PCNC_PACKED __attribute__((packed))

typedef enum {
    FRAME_UPLOAD_DATA = 1,
    FRAME_UPLOAD_ACK = 2,
    FRAME_DOWNLOAD_DATA = 3,
    FRAME_DOWNLOAD_ACK = 4,
    FRAME_CMD = 5,
    FRAME_RESP = 6,
    FRAME_EVENT = 7,
} FrameType;

typedef enum {
    CMD_PING = 1,
    CMD_INFO = 2,
    CMD_STATUS = 3,
    CMD_HOME = 4,
    CMD_PROBE_Z = 5,
    CMD_JOG = 6,
    CMD_JOG_CANCEL = 7,
    CMD_ZERO = 8,
    CMD_START = 9,
    CMD_PAUSE = 10,
    CMD_RESUME = 11,
    CMD_ABORT = 12,
    CMD_ESTOP = 13,
    CMD_RESET = 14,
    CMD_SPINDLE_ON = 15,
    CMD_SPINDLE_OFF = 16,
    CMD_OVERRIDE = 17,
    CMD_FILE_LIST = 18,
    CMD_FILE_LOAD = 19,
    CMD_FILE_UNLOAD = 20,
    CMD_FILE_DELETE = 21,
    CMD_FILE_UPLOAD = 22,
    CMD_FILE_UPLOAD_END = 23,
    CMD_FILE_UPLOAD_ABORT = 24,
    CMD_FILE_DOWNLOAD = 25,
    CMD_FILE_DOWNLOAD_ACK = 26,
    CMD_FILE_DOWNLOAD_ABORT = 27,
    CMD_BEGIN_JOB = 28,
    CMD_END_JOB = 29,
    CMD_CLEAR_JOB = 30,
    CMD_SETTINGS_GET = 31,
    CMD_SETTINGS_SET = 32,
} CommandMessageType;

typedef enum {
    RESP_PONG = 1,
    RESP_INFO = 2,
    RESP_STATE = 3,
    RESP_CAPS = 4,
    RESP_SAFETY = 5,
    RESP_JOB = 6,
    RESP_POS = 7,
    RESP_COMMAND_ACK = 8,
    RESP_ERROR = 9,
    RESP_FILE_ENTRY = 10,
    RESP_FILE_LIST_END = 11,
    RESP_FILE_LOAD = 12,
    RESP_FILE_UNLOAD = 13,
    RESP_FILE_DELETE = 14,
    RESP_FILE_UPLOAD_READY = 15,
    RESP_FILE_UPLOAD_END = 16,
    RESP_FILE_UPLOAD_ABORT = 17,
    RESP_FILE_DOWNLOAD_READY = 18,
    RESP_FILE_DOWNLOAD_END = 19,
    RESP_FILE_DOWNLOAD_ABORT = 20,
    RESP_STORAGE_ERROR = 21,
    RESP_WAIT = 22,
    RESP_MACHINE_SETTINGS = 23,
} ResponseMessageType;

typedef enum {
    EVENT_STATE = 1,
    EVENT_CAPS = 2,
    EVENT_SAFETY = 3,
    EVENT_JOB = 4,
    EVENT_POS = 5,
    EVENT_STATE_CHANGED = 6,
    EVENT_JOB_PROGRESS = 7,
    EVENT_JOB_COMPLETE = 8,
    EVENT_JOB_ERROR = 9,
    EVENT_SD_MOUNTED = 10,
    EVENT_SD_REMOVED = 11,
    EVENT_TEENSY_CONNECTED = 12,
    EVENT_TEENSY_DISCONNECTED = 13,
    EVENT_ESTOP_ACTIVE = 14,
    EVENT_ESTOP_CLEARED = 15,
    EVENT_LIMIT = 16,
    EVENT_STORAGE_UPLOAD_PROFILE = 17,
    EVENT_STORAGE_UPLOAD_CHUNK_PROFILE = 18,
    EVENT_ALARM = 19,
} EventMessageType;

typedef enum {
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2,
} AxisId;

typedef enum {
    AXES_NONE = 0,
    AXES_X = 1u << 0,
    AXES_Y = 1u << 1,
    AXES_Z = 1u << 2,
    AXES_ALL = AXES_X | AXES_Y | AXES_Z,
} AxesMask;

typedef enum {
    OVERRIDE_FEED = 0,
    OVERRIDE_SPINDLE = 1,
    OVERRIDE_RAPID = 2,
} OverrideTarget;

typedef enum {
    MACHINE_BOOTING = 0,
    MACHINE_SYNCING = 1,
    MACHINE_TEENSY_DISCONNECTED = 2,
    MACHINE_IDLE = 3,
    MACHINE_HOMING = 4,
    MACHINE_JOG = 5,
    MACHINE_STARTING = 6,
    MACHINE_RUNNING = 7,
    MACHINE_HOLD = 8,
    MACHINE_FAULT = 9,
    MACHINE_ESTOP = 10,
    MACHINE_COMMS_FAULT = 11,
    MACHINE_UPLOADING = 12,
} ProtocolMachineState;

typedef enum {
    SAFETY_SAFE = 0,
    SAFETY_MONITORING = 1,
    SAFETY_WARNING = 2,
    SAFETY_CRITICAL = 3,
} ProtocolSafetyLevel;

typedef enum {
    CAP_MOTION = 1u << 0,
    CAP_PROBE = 1u << 1,
    CAP_SPINDLE = 1u << 2,
    CAP_FILE_LOAD = 1u << 3,
    CAP_JOB_START = 1u << 4,
    CAP_JOB_PAUSE = 1u << 5,
    CAP_JOB_RESUME = 1u << 6,
    CAP_JOB_ABORT = 1u << 7,
    CAP_OVERRIDES = 1u << 8,
    CAP_RESET = 1u << 9,
} CapabilityFlags;

typedef enum {
    STORAGE_OP_NONE = 0,
    STORAGE_OP_LIST = 1,
    STORAGE_OP_LOAD = 2,
    STORAGE_OP_UNLOAD = 3,
    STORAGE_OP_DELETE = 4,
    STORAGE_OP_UPLOAD = 5,
    STORAGE_OP_DOWNLOAD = 6,
} ProtocolStorageOperation;

typedef enum {
    ERROR_NONE = 0,
    ERROR_INVALID_STATE = 1,
    ERROR_MISSING_PARAM = 2,
    ERROR_NO_JOB_LOADED = 3,
    ERROR_UPLOAD_FILE_EXISTS = 4,
    ERROR_STORAGE_BUSY = 5,
    ERROR_STORAGE_NOT_ALLOWED = 6,
    ERROR_STORAGE_NO_SD = 7,
    ERROR_STORAGE_FILE_NOT_FOUND = 8,
    ERROR_STORAGE_INVALID_FILENAME = 9,
    ERROR_STORAGE_INVALID_SESSION = 10,
    ERROR_STORAGE_BAD_SEQUENCE = 11,
    ERROR_STORAGE_SIZE_MISMATCH = 12,
    ERROR_STORAGE_CRC_FAIL = 13,
    ERROR_STORAGE_READ_FAIL = 14,
    ERROR_STORAGE_WRITE_FAIL = 15,
    ERROR_STORAGE_NO_SPACE = 16,
    ERROR_STORAGE_ABORTED = 17,
    ERROR_DOWNLOAD_MISSING_PARAM = 18,
    ERROR_UPLOAD_MISSING_PARAM = 19,
    ERROR_UNKNOWN = 255,
} ProtocolErrorCode;

typedef struct PCNC_PACKED {
    uint8_t type;
    uint8_t transfer_id;
    uint8_t flags;
    uint32_t seq;
    uint16_t payload_len;
} ProtocolFrameHeader;

typedef struct PCNC_PACKED { uint8_t message_type; } CmdPing;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdInfo;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdStatus;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdHome;
typedef struct PCNC_PACKED { uint8_t message_type; float depth; uint16_t feed; } CmdProbeZ;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t axis; float dist; uint16_t feed; } CmdJog;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdJogCancel;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t axes_mask; } CmdZero;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdStart;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdPause;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdResume;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdAbort;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdEstop;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdReset;
typedef struct PCNC_PACKED { uint8_t message_type; uint16_t rpm; } CmdSpindleOn;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdSpindleOff;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t target; uint16_t percent; } CmdOverride;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdFileList;
typedef struct PCNC_PACKED { uint8_t message_type; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileLoad;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdFileUnload;
typedef struct PCNC_PACKED { uint8_t message_type; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileDelete;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t size; uint8_t overwrite; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileUpload;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t crc32; } CmdFileUploadEnd;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdFileUploadAbort;
typedef struct PCNC_PACKED { uint8_t message_type; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileDownload;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t transfer_id; uint32_t seq; } CmdFileDownloadAck;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdFileDownloadAbort;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdBeginJob;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t lines; } CmdEndJob;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdClearJob;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdSettingsGet;
typedef struct PCNC_PACKED {
    uint8_t message_type;
    float steps_per_mm_x;
    float steps_per_mm_y;
    float steps_per_mm_z;
    float max_feed_rate_x;
    float max_feed_rate_y;
    float max_feed_rate_z;
    float acceleration_x;
    float acceleration_y;
    float acceleration_z;
    float max_travel_x;
    float max_travel_y;
    float max_travel_z;
    uint8_t soft_limits_enabled;
    uint8_t hard_limits_enabled;
    float spindle_min_rpm;
    float spindle_max_rpm;
    float warning_temperature;
    float max_temperature;
} CmdSettingsSet;

typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; } RespPong;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; char firmware[PCNC_MAX_FIRMWARE_BYTES]; char board[PCNC_MAX_BOARD_BYTES]; uint8_t teensy_connected; } RespInfo;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t state; } RespState;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint16_t caps; } RespCaps;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t safety; } RespSafety;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t has_job; char name[PCNC_MAX_FILENAME_BYTES]; } RespJob;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; float mx; float my; float mz; float wx; float wy; float wz; } RespPos;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t command_type; } RespCommandAck;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t error; char reason[PCNC_MAX_REASON_BYTES]; } RespError;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; char name[PCNC_MAX_FILENAME_BYTES]; uint32_t size; } RespFileEntry;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint32_t count; uint64_t free_bytes; } RespFileListEnd;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; char name[PCNC_MAX_FILENAME_BYTES]; } RespFileLoad;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; } RespFileUnload;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; char name[PCNC_MAX_FILENAME_BYTES]; } RespFileDelete;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t transfer_id; uint32_t size; uint16_t chunk_size; char name[PCNC_MAX_FILENAME_BYTES]; } RespFileUploadReady;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t transfer_id; uint32_t size; char name[PCNC_MAX_FILENAME_BYTES]; } RespFileUploadEnd;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; } RespFileUploadAbort;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t transfer_id; uint32_t size; uint16_t chunk_size; char name[PCNC_MAX_FILENAME_BYTES]; } RespFileDownloadReady;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t transfer_id; uint32_t crc32; char name[PCNC_MAX_FILENAME_BYTES]; } RespFileDownloadEnd;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; } RespFileDownloadAbort;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t error; uint8_t operation; uint32_t seq; uint32_t expected; uint32_t actual; char detail[PCNC_MAX_REASON_BYTES]; } RespStorageError;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; char reason[PCNC_MAX_REASON_BYTES]; } RespWait;
typedef struct PCNC_PACKED {
    uint8_t message_type;
    uint32_t request_seq;
    float steps_per_mm_x;
    float steps_per_mm_y;
    float steps_per_mm_z;
    float max_feed_rate_x;
    float max_feed_rate_y;
    float max_feed_rate_z;
    float acceleration_x;
    float acceleration_y;
    float acceleration_z;
    float max_travel_x;
    float max_travel_y;
    float max_travel_z;
    uint8_t soft_limits_enabled;
    uint8_t hard_limits_enabled;
    float spindle_min_rpm;
    float spindle_max_rpm;
    float warning_temperature;
    float max_temperature;
} RespMachineSettings;

typedef struct PCNC_PACKED { uint8_t message_type; uint8_t state; } EventState;
typedef struct PCNC_PACKED { uint8_t message_type; uint16_t caps; } EventCaps;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t safety; } EventSafety;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t has_job; char name[PCNC_MAX_FILENAME_BYTES]; } EventJob;
typedef struct PCNC_PACKED { uint8_t message_type; float mx; float my; float mz; float wx; float wy; float wz; } EventPos;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t state; } EventStateChanged;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t line; uint32_t total; } EventJobProgress;
typedef struct PCNC_PACKED { uint8_t message_type; } EventJobComplete;
typedef struct PCNC_PACKED { uint8_t message_type; char reason[PCNC_MAX_REASON_BYTES]; } EventJobError;
typedef struct PCNC_PACKED { uint8_t message_type; } EventSdMounted;
typedef struct PCNC_PACKED { uint8_t message_type; } EventSdRemoved;
typedef struct PCNC_PACKED { uint8_t message_type; } EventTeensyConnected;
typedef struct PCNC_PACKED { uint8_t message_type; } EventTeensyDisconnected;
typedef struct PCNC_PACKED { uint8_t message_type; } EventEstopActive;
typedef struct PCNC_PACKED { uint8_t message_type; } EventEstopCleared;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t axes_mask; } EventLimit;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t size; uint32_t total_ms; uint32_t prealloc_ms; uint32_t write_ms; uint32_t max_write_ms; uint32_t close_ms; uint32_t chunks; uint32_t queue_max; uint32_t bps; } EventStorageUploadProfile;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t seq; uint32_t bytes; uint32_t total_ms; uint32_t write_ms; uint32_t last_write_ms; uint32_t max_write_ms; uint32_t chunks; uint32_t queue; uint32_t queue_max; uint32_t bps; } EventStorageUploadChunkProfile;
typedef struct PCNC_PACKED { uint8_t message_type; uint16_t code; char message[PCNC_MAX_MESSAGE_BYTES]; } EventAlarm;

PCNC_STATIC_ASSERT(sizeof(ProtocolFrameHeader) == PCNC_FRAME_HEADER_SIZE, "ProtocolFrameHeader must match the wire header");
PCNC_STATIC_ASSERT(sizeof(CmdJog) == 8, "CmdJog wire size changed");
PCNC_STATIC_ASSERT(sizeof(RespPos) == 29, "RespPos wire size changed");

#undef PCNC_PACKED
#undef PCNC_STATIC_ASSERT
