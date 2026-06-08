#include "app/app_bmp280_sensor.h"

#include <Arduino.h>
#include <Wire.h>

#include "board/board_config.h"

extern "C" {
#include "grbl/messages.h"
#include "grbl/task.h"
}

extern "C" void report_message(const char *msg, message_type_t type);

#ifndef CNC_ENABLE_BMP280
#define CNC_ENABLE_BMP280 1
#endif

static constexpr uint8_t kBmp280AddressPrimary = 0x76;
static constexpr uint8_t kBmp280AddressSecondary = 0x77;
static constexpr uint8_t kBmp280ChipIdReg = 0xD0;
static constexpr uint8_t kBmp280CalibReg = 0x88;
static constexpr uint8_t kBmp280ConfigReg = 0xF5;
static constexpr uint8_t kBmp280CtrlMeasReg = 0xF4;
static constexpr uint8_t kBmp280TempMsbReg = 0xFA;
static constexpr uint8_t kBmp280ChipId = 0x58;
static constexpr uint8_t kBme280ChipId = 0x60;
static constexpr uint32_t kBmp280PollMs = 2000;
static constexpr uint8_t kBmp280MaxFailedReads = 3;

static bool sensor_present = false;
static bool sensor_configured = false;
static uint8_t sensor_address = kBmp280AddressPrimary;
static uint16_t dig_t1 = 0;
static int16_t dig_t2 = 0;
static int16_t dig_t3 = 0;
static float latest_temperature_c = 0.0f;
static bool latest_temperature_valid = false;
static uint8_t consecutive_failed_reads = 0;
static const char *status_message = "BMP280 not started";
static uint8_t status_type = Message_Warning;

static bool read_temperature(float *temperature_c);

static bool write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool read_registers(uint8_t address, uint8_t reg, uint8_t *buffer, uint8_t length)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    if(Wire.endTransmission(false) != 0)
        return false;

    const uint8_t read = Wire.requestFrom(address, length);
    if(read != length)
        return false;

    for(uint8_t i = 0; i < length; i++)
        buffer[i] = Wire.read();

    return true;
}

static bool read_u8(uint8_t address, uint8_t reg, uint8_t *value)
{
    return read_registers(address, reg, value, 1);
}

static uint16_t u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t s16_le(const uint8_t *data)
{
    return (int16_t)u16_le(data);
}

static bool probe_address(uint8_t address)
{
    uint8_t chip_id = 0;

    if(!read_u8(address, kBmp280ChipIdReg, &chip_id))
        return false;

    return chip_id == kBmp280ChipId || chip_id == kBme280ChipId;
}

static bool load_calibration()
{
    uint8_t calibration[6] = {0};

    if(!read_registers(sensor_address, kBmp280CalibReg, calibration, sizeof(calibration)))
        return false;

    dig_t1 = u16_le(&calibration[0]);
    dig_t2 = s16_le(&calibration[2]);
    dig_t3 = s16_le(&calibration[4]);

    return dig_t1 != 0;
}

static bool configure_sensor()
{
    if(!load_calibration())
        return false;

    if(!write_register(sensor_address, kBmp280ConfigReg, 0xA0))
        return false;

    return write_register(sensor_address, kBmp280CtrlMeasReg, 0x27);
}

static bool detect_and_configure_sensor()
{
    sensor_present = false;
    sensor_configured = false;

    if(probe_address(kBmp280AddressPrimary)) {
        sensor_address = kBmp280AddressPrimary;
        sensor_present = true;
    } else if(probe_address(kBmp280AddressSecondary)) {
        sensor_address = kBmp280AddressSecondary;
        sensor_present = true;
    }

    if(!sensor_present) {
        latest_temperature_valid = false;
        consecutive_failed_reads = kBmp280MaxFailedReads;
        status_message = "BMP280 not detected";
        status_type = Message_Warning;
        return false;
    }

    sensor_configured = configure_sensor();
    if(!sensor_configured) {
        latest_temperature_valid = false;
        consecutive_failed_reads = kBmp280MaxFailedReads;
        status_message = "BMP280 detected but configuration failed";
        status_type = Message_Warning;
        return false;
    }

    consecutive_failed_reads = 0;
    status_message = sensor_address == kBmp280AddressPrimary ? "BMP280 ready at 0x76" : "BMP280 ready at 0x77";
    status_type = Message_Info;
    return true;
}

static void mark_read_failed()
{
    if(consecutive_failed_reads < kBmp280MaxFailedReads)
        consecutive_failed_reads++;

    if(consecutive_failed_reads >= kBmp280MaxFailedReads) {
        latest_temperature_valid = false;
        status_message = "BMP280 temperature unavailable";
        status_type = Message_Warning;
    }
}

static bool update_temperature()
{
    float temperature_c = 0.0f;

    if(!sensor_present || !sensor_configured) {
        if(!detect_and_configure_sensor())
            return false;
    }

    if(read_temperature(&temperature_c)) {
        latest_temperature_c = temperature_c;
        latest_temperature_valid = true;
        consecutive_failed_reads = 0;
        status_message = sensor_address == kBmp280AddressPrimary ? "BMP280 ready at 0x76" : "BMP280 ready at 0x77";
        status_type = Message_Info;
        return true;
    }

    mark_read_failed();
    return false;
}

static bool read_temperature(float *temperature_c)
{
    uint8_t data[3] = {0};

    if(!read_registers(sensor_address, kBmp280TempMsbReg, data, sizeof(data)))
        return false;

    const int32_t adc_t = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((int32_t)data[2] >> 4);
    const int32_t adc_t_shifted = adc_t >> 4;
    const int32_t var1 = ((((adc_t >> 3) - ((int32_t)dig_t1 << 1))) * (int32_t)dig_t2) >> 11;
    const int32_t var2 = (((((adc_t_shifted - (int32_t)dig_t1) *
                             (adc_t_shifted - (int32_t)dig_t1)) >> 12) *
                           (int32_t)dig_t3) >> 14);
    const int32_t t_fine = var1 + var2;
    const int32_t temp_centi_c = (t_fine * 5 + 128) >> 8;

    *temperature_c = (float)temp_centi_c / 100.0f;
    return true;
}

static void bmp280_poll_task(void *data)
{
    (void)data;

#if CNC_ENABLE_BMP280
    (void)update_temperature();

    task_add_delayed(bmp280_poll_task, nullptr, kBmp280PollMs);
#endif
}

void app_bmp280_sensor_start(void)
{
#if CNC_ENABLE_BMP280
    Wire.begin();
    Wire.setClock(100000);

    if(detect_and_configure_sensor())
        (void)update_temperature();

    report_message(status_message, (message_type_t)status_type);

    task_add_delayed(bmp280_poll_task, nullptr, kBmp280PollMs);
#endif
}

bool app_bmp280_sensor_temperature_c(float *temperature_c)
{
    if(temperature_c == nullptr || !latest_temperature_valid)
        return false;

    *temperature_c = latest_temperature_c;
    return true;
}

bool app_bmp280_sensor_status(const char **message, uint8_t *type)
{
    if(message == nullptr || type == nullptr)
        return false;

    *message = status_message;
    *type = status_type;
    return status_message != nullptr && status_message[0] != '\0';
}
