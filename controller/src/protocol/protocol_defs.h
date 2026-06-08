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
    PCNC_CMD_PING = 1,
    PCNC_CMD_INFO = 2,
    PCNC_CMD_STATUS = 3,
    PCNC_CMD_HOME = 4,
    PCNC_CMD_PROBE_Z = 5,
    PCNC_CMD_JOG = 6,
    PCNC_CMD_JOG_CANCEL = 7,
    PCNC_CMD_ZERO = 8,
    PCNC_CMD_START = 9,
    PCNC_CMD_PAUSE = 10,
    PCNC_CMD_RESUME = 11,
    PCNC_CMD_ABORT = 12,
    PCNC_CMD_ESTOP = 13,
    PCNC_CMD_RESET = 14,
    PCNC_CMD_SPINDLE_ON = 15,
    PCNC_CMD_SPINDLE_OFF = 16,
    PCNC_CMD_OVERRIDE = 17,
    PCNC_CMD_FILE_LIST = 18,
    PCNC_CMD_FILE_LOAD = 19,
    PCNC_CMD_FILE_UNLOAD = 20,
    PCNC_CMD_FILE_DELETE = 21,
    PCNC_CMD_FILE_UPLOAD = 22,
    PCNC_CMD_FILE_UPLOAD_END = 23,
    PCNC_CMD_FILE_UPLOAD_ABORT = 24,
    PCNC_CMD_FILE_DOWNLOAD = 25,
    PCNC_CMD_RESERVED_26 = 26,
    PCNC_CMD_FILE_DOWNLOAD_ABORT = 27,
    PCNC_CMD_BEGIN_JOB = 28,
    PCNC_CMD_END_JOB = 29,
    PCNC_CMD_CLEAR_JOB = 30,
    PCNC_CMD_SETTINGS_GET = 31,
    PCNC_CMD_SETTINGS_SET = 32,
    PCNC_CMD_UNLOCK = 33,
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
    RESP_TEMPERATURE = 24,
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
    EVENT_RESERVED_12 = 12,
    EVENT_RESERVED_13 = 13,
    EVENT_ESTOP_ACTIVE = 14,
    EVENT_ESTOP_CLEARED = 15,
    EVENT_LIMIT = 16,
    EVENT_RESERVED_17 = 17,
    EVENT_RESERVED_18 = 18,
    EVENT_ALARM = 19,
    EVENT_TEMPERATURE = 20,
    EVENT_MESSAGE = 21,
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
    MACHINE_RESERVED_2 = 2,
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
typedef struct PCNC_PACKED { uint8_t message_type; float depth; uint16_t feed; } CmdProbeZ;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t axis; float dist; uint16_t feed; } CmdJog;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t axes_mask; } CmdZero;
typedef struct PCNC_PACKED { uint8_t message_type; uint16_t rpm; } CmdSpindleOn;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t target; uint16_t percent; } CmdOverride;
typedef struct PCNC_PACKED { uint8_t message_type; } CmdUnlock;
typedef struct PCNC_PACKED { uint8_t message_type; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileLoad;
typedef struct PCNC_PACKED { uint8_t message_type; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileDelete;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t size; uint8_t overwrite; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileUpload;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t crc32; } CmdFileUploadEnd;
typedef struct PCNC_PACKED { uint8_t message_type; char name[PCNC_MAX_FILENAME_BYTES]; } CmdFileDownload;

typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; } RespPong;
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; char firmware[PCNC_MAX_FIRMWARE_BYTES]; char board[PCNC_MAX_BOARD_BYTES]; } RespInfo;
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
typedef struct PCNC_PACKED { uint8_t message_type; uint32_t request_seq; uint8_t has_temperature; float temperature_c; } RespTemperature;

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
typedef struct PCNC_PACKED { uint8_t message_type; } EventEstopActive;
typedef struct PCNC_PACKED { uint8_t message_type; } EventEstopCleared;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t axes_mask; uint8_t min_axes_mask; uint8_t max_axes_mask; } EventLimit;
typedef struct PCNC_PACKED { uint8_t message_type; uint16_t code; char message[PCNC_MAX_MESSAGE_BYTES]; } EventAlarm;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t has_temperature; float temperature_c; } EventTemperature;
typedef struct PCNC_PACKED { uint8_t message_type; uint8_t message_level; char message[PCNC_MAX_MESSAGE_BYTES]; } EventMessage;

PCNC_STATIC_ASSERT(sizeof(ProtocolFrameHeader) == PCNC_FRAME_HEADER_SIZE, "ProtocolFrameHeader must match the wire header");
PCNC_STATIC_ASSERT(sizeof(CmdJog) == 8, "CmdJog wire size changed");
PCNC_STATIC_ASSERT(sizeof(RespPos) == 29, "RespPos wire size changed");
PCNC_STATIC_ASSERT(sizeof(RespTemperature) == 10, "RespTemperature wire size changed");
PCNC_STATIC_ASSERT(sizeof(EventTemperature) == 6, "EventTemperature wire size changed");
PCNC_STATIC_ASSERT(sizeof(EventMessage) == 66, "EventMessage wire size changed");

#undef PCNC_PACKED
#undef PCNC_STATIC_ASSERT
