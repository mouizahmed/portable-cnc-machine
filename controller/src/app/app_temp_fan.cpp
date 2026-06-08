#include "app/app_temp_fan.h"

#include <Arduino.h>

#include "app/app_bmp280_sensor.h"
#include "board/board_config.h"
#include "board/pins.h"

extern "C" {
#include "grbl/task.h"
}

#ifndef CNC_ENABLE_TEMP_FAN
#define CNC_ENABLE_TEMP_FAN 1
#endif

#ifndef CNC_TEMP_FAN_ON_C
#define CNC_TEMP_FAN_ON_C 35.0f
#endif

#ifndef CNC_TEMP_FAN_OFF_C
#define CNC_TEMP_FAN_OFF_C 30.0f
#endif

#ifndef CNC_PIN_FAN
#define CNC_PIN_FAN -1
#endif

#ifndef CNC_PIN_FAN_ON
#define CNC_PIN_FAN_ON 1
#endif

static constexpr uint32_t kTempFanPollMs = 500;

static bool fan_on = false;

static void set_fan(bool on)
{
#if CNC_ENABLE_TEMP_FAN && CNC_PIN_FAN >= 0
    if(fan_on == on)
        return;

    fan_on = on;
    digitalWrite(CNC_PIN_FAN, on == (CNC_PIN_FAN_ON != 0) ? HIGH : LOW);
#else
    (void)on;
#endif
}

static void temp_fan_task(void *data)
{
    (void)data;

#if CNC_ENABLE_TEMP_FAN && CNC_PIN_FAN >= 0
    float temperature_c = 0.0f;
    if(!app_bmp280_sensor_temperature_c(&temperature_c)) {
        set_fan(true);
    } else if(temperature_c >= CNC_TEMP_FAN_ON_C) {
        set_fan(true);
    } else if(temperature_c <= CNC_TEMP_FAN_OFF_C) {
        set_fan(false);
    }

    task_add_delayed(temp_fan_task, nullptr, kTempFanPollMs);
#endif
}

void app_temp_fan_start(void)
{
#if CNC_ENABLE_TEMP_FAN && CNC_PIN_FAN >= 0
    pinMode(CNC_PIN_FAN, OUTPUT);
    set_fan(true);
    task_add_delayed(temp_fan_task, nullptr, kTempFanPollMs);
#endif
}
