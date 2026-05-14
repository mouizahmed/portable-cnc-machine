/*
  encoders.c - quadrature encoders interface (API)

  Part of grblHAL

  Copyright (c) 2026 Terje Io

  grblHAL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grblHAL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grblHAL. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _ENCODERS_H_
#define _ENCODERS_H_

#include "plugins.h"

// Quadrature encoder interface

typedef union {
    uint8_t value;
    uint8_t events;
    struct {
        uint8_t position_changed  :1,
                direction_changed :1,
                click             :1,
                dbl_click         :1,
                long_click        :1,
                index_pulse       :1,
                unused            :2;
    };
} encoder_event_t;

typedef union {
    uint8_t value;
    uint8_t mask;
    struct {
        uint8_t bidirectional :1,
                select        :1,
                index         :1,
                spindle_rpm   :1,
                spindle_pos   :1,
                unused        :3;
    };
} encoder_caps_t;

typedef struct {
    uint32_t vel_timeout;
    uint32_t dbl_click_window;  //!< ms.
} encoder_cfg_t;

typedef struct {
    int32_t position;
    uint32_t velocity;
} encoder_data_t;

struct encoder;
typedef struct encoder encoder_t;

/*! \brief Pointer to callback function to receive encoder events.
\param encoder pointer to a \a encoder_t struct.
\param events pointer to a \a encoder_event_t struct.
\param context pointer to the context passed to the encoders claim function.
*/
typedef void (*encoder_on_event_ptr)(encoder_t *encoder, encoder_event_t *events, void *context);

/*! \brief Pointer to function for resetting encoder data.
\param encoder pointer to a \a encoder_t struct.
*/
typedef void (*encoder_reset_ptr)(encoder_t *encoder);

/*! \brief Pointer to function for claiming an encoder.
\param encoder pointer to a \a encoder_t struct.
\param event_handler pointer to to the event handler callback.
\param context pointer to the context to be passed to event handler.
\returns \a true when claim was successful, \a false to otherwise.
*/
typedef bool (*encoder_claim_ptr)(encoder_t *encoder, encoder_on_event_ptr event_handler, void *context);

/*! \brief Pointer to function for getting encoder data.
\param encoder pointer to a \a encoder_t struct.
\returns pointer to a \a encoder_data_t struct containing the data.
*/
typedef encoder_data_t *(*encoder_get_data_ptr)(encoder_t *encoder);

/*! \brief Pointer to the callbak function to be called by encoders_enumerate().
\param encoder pointer to a \a encoder_t struct.
\returns \a true to stop the enumeration and return true from encoders_enumerate(), \a false otherwise.
*/
typedef bool (*encoder_enumerate_callback_ptr)(encoder_t *encoder, void *data);

/*! \brief Pointer to function for configuring an encoder.
\param encoder pointer to a \a encoder_t struct.
\param encoder pointer to a \a encoder_cfg_t struct.
\returns \a true when claim was successful, \a false to otherwise.
*/
typedef bool (*encoder_configure_ptr)(encoder_t *encoder, encoder_cfg_t *settings);

void encoder_register (encoder_t *encoder);
bool encoders_enumerate (encoder_enumerate_callback_ptr callback, void *data);
uint8_t encoders_get_count (void);

struct encoder {
    void *hw;
    encoder_caps_t caps;
    encoder_claim_ptr claim;
    encoder_reset_ptr reset;
    encoder_get_data_ptr get_data;
    encoder_configure_ptr configure;
};

#endif // _ENCODERS_H_

// Interrupt driven Quadrature Encoder Interface - static code for driver/plugin use

#if QEI_ENABLE && defined(QEI_A_PIN) && defined(QEI_B_PIN)

typedef enum {
   QEI_DirUnknown = 0,
   QEI_DirCW,
   QEI_DirCCW
} qei_dir_t;

typedef union {
    uint_fast8_t pins;
    struct {
        uint_fast8_t a :1,
                     b :1;
    };
} qei_state_t;

typedef struct {
    encoder_t encoder;
    encoder_data_t data;
    encoder_event_t event;
    void *context;
    int32_t vel_count;
    uint_fast16_t state;
    qei_dir_t dir;
    uint8_t port_a, port_b, port_select;
    volatile uint32_t dbl_click_timeout;
    volatile uint32_t vel_timeout;
    uint32_t vel_timestamp;
    encoder_on_event_ptr on_event;
    encoder_cfg_t settings;
} iqei_t;

static iqei_t iqei = {
    .port_a = IOPORT_UNASSIGNED,
    .port_b = IOPORT_UNASSIGNED,
    .port_select = IOPORT_UNASSIGNED,
    .settings.dbl_click_window = 500,
    .encoder.caps.bidirectional = On
};

static void iqei_select_irq (uint8_t port, bool high);

static void iqei_post_event (void *data)
{
    iqei.event.events |= ((encoder_event_t *)data)->events;

    iqei.on_event(&iqei.encoder, &iqei.event, iqei.context);
}

static void iqei_reset (encoder_t *encoder)
{
    iqei.vel_timeout = 0;
    iqei.dir = QEI_DirUnknown;
    iqei.data.position = iqei.vel_count = 0;
    iqei.vel_timestamp = hal.get_elapsed_ticks();
    iqei.vel_timeout = iqei.settings.vel_timeout;
}

static bool iqei_configure (encoder_t *encoder, encoder_cfg_t *settings)
{
    if(iqei.vel_timeout != settings->vel_timeout)
        iqei.vel_timestamp = hal.get_elapsed_ticks();

    memcpy(&iqei.settings, settings, sizeof(encoder_cfg_t));

    return true;
}

static encoder_data_t *iqei_get_data (encoder_t *encoder)
{
    return &iqei.data;
}

static void iqei_poll (void *data)
{
    if(iqei.vel_timeout && !(--iqei.vel_timeout)) {

        uint32_t time = hal.get_elapsed_ticks();

        iqei.data.velocity = abs(iqei.data.position - iqei.vel_count) * 1000 / (time - iqei.vel_timestamp);
        iqei.vel_timestamp = time;
        iqei.vel_timeout = iqei.settings.vel_timeout;
        if((iqei.event.position_changed = !iqei.dbl_click_timeout || iqei.data.velocity == 0))
            iqei.on_event(&iqei.encoder, &iqei.event, iqei.context);
        iqei.vel_count = iqei.data.position;
    }

    if(iqei.dbl_click_timeout && !(--iqei.dbl_click_timeout)) {
        iqei.event.click = On;
        iqei.on_event(&iqei.encoder, &iqei.event, iqei.context);
    }
}

static void iqei_ab_irq (uint8_t port, bool high)
{
    PROGMEM static const uint8_t encoder_valid_state[] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    PROGMEM static const encoder_event_t dir_changed = { .direction_changed = On, .position_changed = On };

    static qei_state_t state = {0};

    if(port == iqei.port_a)
        state.a = high;
    else
        state.b = high;

    uint_fast8_t idx = (((iqei.state << 2) & 0x0F) | state.pins);

    if(encoder_valid_state[idx] ) {

//        int32_t count = iqei.count;

        iqei.state = ((iqei.state << 4) | idx) & 0xFF;

        if(iqei.state == 0x42 || iqei.state == 0xD4 || iqei.state == 0x2B || iqei.state == 0xBD) {
            iqei.data.position--;
            if(iqei.vel_timeout == 0 || iqei.dir == QEI_DirCW) {
                iqei.dir = QEI_DirCCW;
                task_add_immediate(iqei_post_event, &dir_changed);
            }
        } else if(iqei.state == 0x81 || iqei.state == 0x17 || iqei.state == 0xE8 || iqei.state == 0x7E) {
            iqei.data.position++;
            if(iqei.vel_timeout == 0 || iqei.dir == QEI_DirCCW) {
                iqei.dir = QEI_DirCW;
                task_add_immediate(iqei_post_event, &dir_changed);
            }
        }
    }
}

static void iqei_select (void *data)
{
    static uint8_t clicks = 0;

    if(!iqei.dbl_click_timeout) {
        clicks = 1;
        iqei.dbl_click_timeout = iqei.settings.dbl_click_window;
    } else if(iqei.dbl_click_timeout < iqei.settings.dbl_click_window && ++clicks == 2) {
        iqei.dbl_click_timeout = 0;
        iqei.event.dbl_click = On;
        iqei.on_event(&iqei.encoder, &iqei.event, iqei.context);
    }
}

static void iqei_select_irq (uint8_t port, bool high)
{
    static bool lock = false;

    if(high || lock)
        return;

//    lock = true;

    task_add_immediate(iqei_select, NULL);

//    lock = false;
}

static bool iqei_claim (encoder_t *encoder, encoder_on_event_ptr event_handler, void *context)
{
    if(event_handler == NULL || iqei.on_event)
        return false;

    iqei.context = context;
    iqei.on_event = event_handler;
    iqei.encoder.reset = iqei_reset;
    iqei.encoder.get_data = iqei_get_data;
    iqei.encoder.configure = iqei_configure;

    if(iqei.port_b != IOPORT_UNASSIGNED) {
        ioport_enable_irq(iqei.port_a, IRQ_Mode_Change, iqei_ab_irq);
        ioport_enable_irq(iqei.port_b, IRQ_Mode_Change, iqei_ab_irq);
    }

    if(iqei.port_select != IOPORT_UNASSIGNED)
        ioport_enable_irq(iqei.port_select, IRQ_Mode_Change, iqei_select_irq);

    task_add_systick(iqei_poll, NULL);

    return true;
}

static inline void _encoder_pin_claimed (uint8_t port, xbar_t *pin)
{
    switch(pin->function) {

        case Input_QEI_A:
            iqei.port_a = port;
            break;

        case Input_QEI_B:
            iqei.port_b = port;
            iqei.encoder.claim = iqei_claim;
            if(iqei.port_a != IOPORT_UNASSIGNED)
                encoder_register(&iqei.encoder);
            break;

        case Input_QEI_Select:
            iqei.port_select = port;
            iqei.encoder.caps.select = On;
            if(pin->config) {
                gpio_in_config_t config = {
                    .debounce = On,
                    .pull_mode = PullMode_Up
                };
                pin->config(pin, &config, false);
            }
            break;

        default: break;
    }
}

#endif
