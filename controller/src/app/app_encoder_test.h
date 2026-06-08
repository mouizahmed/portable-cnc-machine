#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_encoder_test_start(void);
int32_t app_encoder_test_count(void);
bool app_encoder_test_state(bool *a, bool *b);

#ifdef __cplusplus
}
#endif
