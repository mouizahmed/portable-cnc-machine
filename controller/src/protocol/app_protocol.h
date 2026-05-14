#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void app_protocol_start(void);
void app_protocol_poll_task(void *data);
void app_protocol_push_task(void *data);

#ifdef __cplusplus
}
#endif
