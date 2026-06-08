#include "app/app_encoder_test.h"

#include <Arduino.h>
#include <cstdio>

#include "board/board_config.h"
#include "board/pins.h"

extern "C" {
#include "grbl/messages.h"
#include "grbl/task.h"

void report_message(const char *msg, message_type_t type);
}

#ifndef CNC_ENABLE_ENCODER_TEST
#define CNC_ENABLE_ENCODER_TEST 0
#endif

#ifndef CNC_PIN_ENCODER0_A
#define CNC_PIN_ENCODER0_A -1
#endif

#ifndef CNC_PIN_ENCODER0_B
#define CNC_PIN_ENCODER0_B -1
#endif

static constexpr uint32_t kEncoderReportMs = 250;

static volatile int32_t encoder_count = 0;
static volatile uint8_t encoder_state = 0;
static int32_t last_reported_count = 0;
static uint8_t last_reported_state = 0xff;

static void encoder_changed()
{
#if CNC_ENABLE_ENCODER_TEST && CNC_PIN_ENCODER0_A >= 0 && CNC_PIN_ENCODER0_B >= 0
    const uint8_t a = digitalReadFast(CNC_PIN_ENCODER0_A) ? 1 : 0;
    const uint8_t b = digitalReadFast(CNC_PIN_ENCODER0_B) ? 1 : 0;
    const uint8_t state = (a << 1) | b;
    const uint8_t transition = (encoder_state << 2) | state;

    switch(transition) {
        case 0b0001:
        case 0b0111:
        case 0b1110:
        case 0b1000:
            encoder_count++;
            break;

        case 0b0010:
        case 0b1011:
        case 0b1101:
        case 0b0100:
            encoder_count--;
            break;

        default:
            break;
    }

    encoder_state = state;
#endif
}

static void encoder_report_task(void *data)
{
    (void)data;

#if CNC_ENABLE_ENCODER_TEST && CNC_PIN_ENCODER0_A >= 0 && CNC_PIN_ENCODER0_B >= 0
    noInterrupts();
    const int32_t count = encoder_count;
    const uint8_t state = encoder_state;
    interrupts();

    if(count != last_reported_count || state != last_reported_state) {
        char message[64];
        std::snprintf(message,
                      sizeof(message),
                      "ENC0 count=%ld A=%u B=%u",
                      (long)count,
                      (unsigned)((state >> 1) & 1),
                      (unsigned)(state & 1));
        report_message(message, Message_Info);

        last_reported_count = count;
        last_reported_state = state;
    }

    task_add_delayed(encoder_report_task, nullptr, kEncoderReportMs);
#endif
}

void app_encoder_test_start(void)
{
#if CNC_ENABLE_ENCODER_TEST && CNC_PIN_ENCODER0_A >= 0 && CNC_PIN_ENCODER0_B >= 0
    pinMode(CNC_PIN_ENCODER0_A, INPUT);
    pinMode(CNC_PIN_ENCODER0_B, INPUT);

    const uint8_t a = digitalReadFast(CNC_PIN_ENCODER0_A) ? 1 : 0;
    const uint8_t b = digitalReadFast(CNC_PIN_ENCODER0_B) ? 1 : 0;
    encoder_state = (a << 1) | b;
    last_reported_state = 0xff;

    attachInterrupt(digitalPinToInterrupt(CNC_PIN_ENCODER0_A), encoder_changed, CHANGE);
    attachInterrupt(digitalPinToInterrupt(CNC_PIN_ENCODER0_B), encoder_changed, CHANGE);

    report_message("ENC0 test enabled on pins A=2 B=3", Message_Info);
    task_add_delayed(encoder_report_task, nullptr, kEncoderReportMs);
#endif
}

int32_t app_encoder_test_count(void)
{
    noInterrupts();
    const int32_t count = encoder_count;
    interrupts();
    return count;
}

bool app_encoder_test_state(bool *a, bool *b)
{
#if CNC_ENABLE_ENCODER_TEST && CNC_PIN_ENCODER0_A >= 0 && CNC_PIN_ENCODER0_B >= 0
    noInterrupts();
    const uint8_t state = encoder_state;
    interrupts();

    if(a != nullptr)
        *a = ((state >> 1) & 1) != 0;
    if(b != nullptr)
        *b = (state & 1) != 0;

    return true;
#else
    if(a != nullptr)
        *a = false;
    if(b != nullptr)
        *b = false;

    return false;
#endif
}
