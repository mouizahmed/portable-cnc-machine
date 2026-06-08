#pragma once

// Product-level grblHAL configuration for the Teensy 4.1 controller.
#define BOARD_MY_MACHINE

// Clean boot target: bring up grblHAL first, then enable product features.
#define USB_SERIAL_CDC          2
#define SDCARD_ENABLE           1
#define LITTLEFS_ENABLE         1
#define ETHERNET_ENABLE         0
#define WEBUI_ENABLE            0
#define EEPROM_ENABLE           0
#define DISPLAY_ENABLE          0
#define KEYPAD_ENABLE           0
#define TRINAMIC_ENABLE         0
#define SPINDLE_SYNC_ENABLE     0
#define MODBUS_ENABLE           0
#define ESTOP_ENABLE            0
#define PROBE_ENABLE            0
#define COOLANT_ENABLE          0
#define SPINDLE0_ENABLE         0

// HCT245 common-anode driver wiring:
// ENA+ -> +5V, ENA- -> HCT245 output. A low output activates ENA on the driver,
// and these drivers treat active ENA as disabled, so enabled state must be high.
#define DEFAULT_ENABLE_SIGNALS_INVERT_MASK 0

// Product feature intent. These are consumed by our app layer, not grblHAL.
#define CNC_ENABLE_SD_CARD      1
#define CNC_ENABLE_LITTLEFS     1
#define CNC_ENABLE_TFT          1
#define CNC_ENABLE_TOUCH        1
#define CNC_ENABLE_Z_ALM        1
#define CNC_ENABLE_BMP280       1
#define CNC_ENABLE_TEMP_FAN     1
#define CNC_ENABLE_ENCODER_TEST 1
#define CNC_TEMP_FAN_ON_C       35.0f
#define CNC_TEMP_FAN_OFF_C      30.0f
#define CNC_Z_ALM_ACTIVE_HIGH   1
#define CNC_AXIS_COUNT          3

// Reserve 512 KiB of Teensy flash for app-owned LittleFS data.
#define LFS_SIZE_KB             512
