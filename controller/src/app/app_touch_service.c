#include "app/app_touch_service.h"

#include "app/app_file_service.h"
#include "app/app_ui_controller.h"
#include "app_config/touch_calibration_store.h"
#include "ui/input/touch_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "grbl/messages.h"
#include "grbl/task.h"

void report_message(const char *msg, message_type_t type);

static touch_calibration_t touch_calibration = {0};
static bool touch_calibration_loaded = false;

static int raw_delta(uint16_t a, uint16_t b)
{
    return a > b ? (int)(a - b) : (int)(b - a);
}

static void touch_service_task(void *data)
{
    (void)data;

    static bool have_last = false;
    static touch_raw_t last = {0};
    touch_raw_t sample = {0};

    const bool irq_active = touch_input_irq_active();
    const bool irq_pending = touch_input_take_irq_pending();

    if(!irq_active && !irq_pending) {
        have_last = false;
        task_add_delayed(touch_service_task, NULL, 250);
        return;
    }

    if(touch_input_read_raw(&sample)) {
        if(!have_last ||
            raw_delta(sample.x, last.x) > 24 ||
            raw_delta(sample.y, last.y) > 24 ||
            raw_delta(sample.z, last.z) > 80) {

            uint16_t screen_x = 0;
            uint16_t screen_y = 0;

            if(touch_calibration_loaded &&
               touch_calibration_map_raw(&touch_calibration, sample.x, sample.y, &screen_x, &screen_y))
                app_ui_controller_handle_touch(screen_x, screen_y);

            last = sample;
            have_last = true;
        }
    } else {
        have_last = false;
    }

    task_add_delayed(touch_service_task, NULL, irq_active ? 50 : 250);
}

static void touch_calibration_poll(void *data)
{
    (void)data;

    static const touch_calibration_point_t targets[4] = {
        { .screen_x = 40,  .screen_y = 40,  .raw_x = 0, .raw_y = 0 },
        { .screen_x = 440, .screen_y = 40,  .raw_x = 0, .raw_y = 0 },
        { .screen_x = 440, .screen_y = 280, .raw_x = 0, .raw_y = 0 },
        { .screen_x = 40,  .screen_y = 280, .raw_x = 0, .raw_y = 0 }
    };

    static uint8_t target = 0;
    static uint8_t samples = 0;
    static bool waiting_release = false;
    static uint32_t sum_x = 0;
    static uint32_t sum_y = 0;

    touch_raw_t sample = {0};
    const bool irq_active = touch_input_irq_active();
    const bool touched = irq_active && touch_input_read_raw(&sample);

    if(waiting_release) {
        if(!touched) {
            waiting_release = false;
            samples = 0;
            sum_x = 0;
            sum_y = 0;
            target++;

            if(target >= 4) {
                if(touch_calibration_save(&touch_calibration)) {
                    touch_calibration_loaded = true;
                    report_message("Touch calibration saved", Message_Info);
                    app_ui_controller_show_current();
                    task_add_delayed(touch_service_task, NULL, 500);
                } else
                    report_message("Touch calibration save failed", Message_Warning);
                return;
            }

            app_ui_controller_show_calibration_target(target, targets[target].screen_x, targets[target].screen_y);
        }

        task_add_delayed(touch_calibration_poll, NULL, 80);
        return;
    }

    if(touched) {
        sum_x += sample.x;
        sum_y += sample.y;
        samples++;

        if(samples >= 6) {
            touch_calibration.points[target] = targets[target];
            touch_calibration.points[target].raw_x = (uint16_t)(sum_x / samples);
            touch_calibration.points[target].raw_y = (uint16_t)(sum_y / samples);

            static char message[96];
            snprintf(message, sizeof(message), "Cal point %u raw x=%u y=%u",
                     target + 1, touch_calibration.points[target].raw_x, touch_calibration.points[target].raw_y);
            report_message(message, Message_Info);

            waiting_release = true;
        }
    }

    task_add_delayed(touch_calibration_poll, NULL, 80);
}

void app_touch_service_start(void)
{
#if CNC_ENABLE_TOUCH
    touch_input_init();
    if(touch_calibration_load(&touch_calibration)) {
        touch_calibration_loaded = true;
        report_message("Touch calibration loaded", Message_Info);
        app_file_service_refresh(false);
        app_ui_controller_show_current();
        task_add_delayed(touch_service_task, NULL, 250);
    } else {
        report_message("Touch calibration required", Message_Info);
        app_ui_controller_show_calibration_target(0, 40, 40);
        task_add_delayed(touch_calibration_poll, NULL, 250);
    }
#endif
}
