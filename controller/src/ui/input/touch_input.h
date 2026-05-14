#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t z;
} touch_raw_t;

bool touch_input_init(void);
bool touch_input_read_raw(touch_raw_t *sample);
bool touch_input_irq_active(void);
bool touch_input_take_irq_pending(void);

#ifdef __cplusplus
}
#endif
