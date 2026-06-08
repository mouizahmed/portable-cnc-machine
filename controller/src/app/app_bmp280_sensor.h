#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_bmp280_sensor_start(void);
bool app_bmp280_sensor_temperature_c(float *temperature_c);
bool app_bmp280_sensor_status(const char **message, uint8_t *type);

#ifdef __cplusplus
}
#endif
