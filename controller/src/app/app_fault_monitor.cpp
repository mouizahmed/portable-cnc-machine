#include "app/app_fault_monitor.h"

#include <Arduino.h>

#include "board/pins.h"

extern "C" {
#include "grbl/alarms.h"
#include "grbl/hal.h"
#include "grbl/system.h"
#include "grbl/task.h"
}

#ifndef CNC_ENABLE_Z_ALM
#define CNC_ENABLE_Z_ALM 1
#endif

#ifndef CNC_Z_ALM_ACTIVE_HIGH
#define CNC_Z_ALM_ACTIVE_HIGH 1
#endif

static constexpr uint32_t kFaultPollMs = 25;
static constexpr uint32_t kFaultStartupDelayMs = 1500;
static constexpr uint8_t kFaultTripSamples = 4;

static uint8_t z_fault_samples = 0;

static bool z_fault_active()
{
#if CNC_ENABLE_Z_ALM && CNC_PIN_Z_ALM >= 0
    const bool high = digitalRead(CNC_PIN_Z_ALM) == HIGH;
    return CNC_Z_ALM_ACTIVE_HIGH ? high : !high;
#else
    return false;
#endif
}

static void z_fault_monitor_task(void *data)
{
    (void)data;

    if(z_fault_active()) {
        if(z_fault_samples < kFaultTripSamples)
            z_fault_samples++;
    } else {
        z_fault_samples = 0;
    }

    if(z_fault_samples >= kFaultTripSamples)
        system_set_exec_alarm(Alarm_MotorFault);

    task_add_delayed(z_fault_monitor_task, nullptr, kFaultPollMs);
}

void app_fault_monitor_start(void)
{
#if CNC_ENABLE_Z_ALM && CNC_PIN_Z_ALM >= 0
    pinMode(CNC_PIN_Z_ALM, CNC_Z_ALM_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
    task_add_delayed(z_fault_monitor_task, nullptr, kFaultStartupDelayMs);
#endif
}
