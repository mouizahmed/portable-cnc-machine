/*
  encoders.c - quadrature encoder interface (API)

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

#include <stdint.h>
#include <stdbool.h>

#include "hal.h"
#include "encoders.h"

static struct encoders {
    encoder_t *encoder;
    struct encoders *next;
} *encoders = NULL;

static uint8_t n_encoders = 0;

void encoder_register (encoder_t *encoder)
{
    struct encoders *add, *last;

    if((add = malloc(sizeof(struct encoders)))) {

       add->next = NULL;
       add->encoder = encoder;

        if((last = encoders)) {
            while(last->next)
                last = last->next;
            last->next = add;
        } else
            encoders = add;

        n_encoders++;
    }
}

bool encoders_enumerate (encoder_enumerate_callback_ptr callback, void *data)
{
    bool ok = false;
    struct encoders *encoder = encoders;

    if(encoder) do {
        ok = callback(encoder->encoder, data);
    } while(!ok && (encoder = encoder->next));

    return ok;
}

uint8_t encoders_get_count (void)
{
    return n_encoders;
}
