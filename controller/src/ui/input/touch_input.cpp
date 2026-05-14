#include "ui/input/touch_input.h"

#include <Arduino.h>
#include <SPI.h>

#include "board/pins.h"

#define XPT2046_CMD_X   0xD0
#define XPT2046_CMD_Y   0x90
#define XPT2046_CMD_Z1  0xB0
#define XPT2046_CMD_Z2  0xC0

static bool initialized = false;
static volatile bool irq_pending = false;

static void touch_irq_handler(void)
{
    irq_pending = true;
}

static uint16_t read12(uint8_t command)
{
    SPI.transfer(command);
    const uint16_t hi = SPI.transfer(0x00);
    const uint16_t lo = SPI.transfer(0x00);

    return ((hi << 8) | lo) >> 3;
}

bool touch_input_init(void)
{
    if(initialized)
        return true;

    pinMode(CNC_PIN_TOUCH_CS, OUTPUT);
    digitalWrite(CNC_PIN_TOUCH_CS, HIGH);

    pinMode(CNC_PIN_TOUCH_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(CNC_PIN_TOUCH_IRQ), touch_irq_handler, FALLING);

    SPI.setMOSI(CNC_PIN_TOUCH_DIN);
    SPI.setMISO(CNC_PIN_TOUCH_DO);
    SPI.setSCK(CNC_PIN_TOUCH_CLK);
    SPI.begin();

    initialized = true;
    irq_pending = digitalRead(CNC_PIN_TOUCH_IRQ) == LOW;

    return true;
}

bool touch_input_irq_active(void)
{
    return touch_input_init() && digitalRead(CNC_PIN_TOUCH_IRQ) == LOW;
}

bool touch_input_take_irq_pending(void)
{
    if(!touch_input_init())
        return false;

    noInterrupts();
    const bool pending = irq_pending;
    irq_pending = false;
    interrupts();

    return pending;
}

bool touch_input_read_raw(touch_raw_t *sample)
{
    if(sample == NULL || !touch_input_init())
        return false;

    const bool irq_active = touch_input_irq_active();

    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CNC_PIN_TOUCH_CS, LOW);

    const uint16_t z1 = read12(XPT2046_CMD_Z1);
    const uint16_t z2 = read12(XPT2046_CMD_Z2);
    const uint16_t x1 = read12(XPT2046_CMD_X);
    const uint16_t y1 = read12(XPT2046_CMD_Y);
    const uint16_t x2 = read12(XPT2046_CMD_X);
    const uint16_t y2 = read12(XPT2046_CMD_Y);

    digitalWrite(CNC_PIN_TOUCH_CS, HIGH);
    SPI.endTransaction();

    int pressure = (int)z1 + 4095 - (int)z2;
    if(pressure < 0)
        pressure = 0;
    if(pressure > 4095)
        pressure = 4095;

    sample->x = (x1 + x2) / 2u;
    sample->y = (y1 + y2) / 2u;
    sample->z = (uint16_t)pressure;

    return irq_active || sample->z > 40u;
}
