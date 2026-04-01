#include "app/portable_cnc_app.h"

#include "config.h"
#include "pico/stdlib.h"

PortableCncApp::PortableCncApp(Ili9488& display, Xpt2046& touch, SdSpiCard& sd_card, CalibrationStorage& storage)
    : touch_(touch),
      storage_service_(sd_card),
      status_provider_(machine_state_machine_, jog_state_machine_, job_state_machine_, storage_service_),
      calibration_app_(display, touch, storage),
      frame_(display),
      main_menu_screen_(display, frame_),
      jog_screen_(display, frame_, jog_state_machine_),
      files_screen_(display, frame_, job_state_machine_) {
    router_.register_screen(main_menu_screen_);
    router_.register_screen(jog_screen_);
    router_.register_screen(files_screen_);
    router_.navigate_to(NavTab::Home);
}

void PortableCncApp::run() {
    run_startup_sequence();
    render_current_screen(true);

    while (true) {
        if (storage_service_.poll(job_state_machine_)) {
            render_storage_change();
        }

        UiEvent event{};
        if (poll_event(event)) {
            handle_event(event);
        }

        sleep_ms(UI_POLL_MS);
    }
}

void PortableCncApp::run_startup_sequence() {
    TouchCalibration calibration{};
    machine_state_machine_.handle_event(MachineEvent::StartCalibration);
    calibration_app_.ensure_calibration(calibration);
    machine_state_machine_.handle_event(MachineEvent::CalibrationCompleted);
    storage_service_.initialize(job_state_machine_);
}

bool PortableCncApp::poll_event(UiEvent& event) {
    TouchPoint point{};
    const bool touched = touch_.read_touch(point);
    if (touched && !touch_latched_) {
        touch_latched_ = true;
        last_touch_point_ = point;
        event = UiEvent{UiEventType::TouchPressed, point};
        return true;
    }

    if (touched) {
        last_touch_point_ = point;
    }

    if (!touched && touch_latched_) {
        touch_latched_ = false;
        event = UiEvent{UiEventType::TouchReleased, last_touch_point_};
        return true;
    }

    if (!touched) {
        touch_latched_ = false;
    }

    return false;
}

void PortableCncApp::handle_event(const UiEvent& event) {
    UiEventResult result = frame_.handle_event(event);
    if (!result.handled) {
        result = router_.current().handle_event(event);
    }

    if (result.refresh_status_bar) {
        frame_.render_status_bar(status_provider_.current());
    }

    if (!result.has_navigation) {
        return;
    }

    if (router_.can_navigate_to(result.navigation_target) &&
        router_.navigate_to(result.navigation_target)) {
        render_current_screen(false);
    }
}

void PortableCncApp::render_storage_change() {
    const StatusSnapshot status = status_provider_.current();
    frame_.render_status_bar(status);

    if (router_.current().tab() == NavTab::Files) {
        files_screen_.refresh_storage_view();
    }
}

void PortableCncApp::render_current_screen(bool full) {
    if (full) {
        router_.current().render(status_provider_.current());
        return;
    }

    frame_.render_nav_bar(router_.current().tab());
    frame_.clear_content();
    router_.current().render_content();
}
