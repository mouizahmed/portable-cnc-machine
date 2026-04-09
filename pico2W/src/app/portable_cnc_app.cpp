#include "app/portable_cnc_app.h"

#include "config.h"
#include "pico/stdlib.h"

PortableCncApp::PortableCncApp(Ili9488& display, Xpt2046& touch, SdSpiCard& sd_card, CalibrationStorage& storage)
    : touch_(touch),
      storage_service_(sd_card),
      controller_(machine_state_machine_, jog_state_machine_, job_state_machine_, storage_service_),
      status_provider_(machine_state_machine_, jog_state_machine_, job_state_machine_, storage_service_),
      calibration_app_(display, touch, storage),
      frame_(display),
      main_menu_screen_(display, frame_, controller_),
      jog_screen_(display, frame_, controller_),
      files_screen_(display, frame_, controller_),
      web_server_(controller_, status_provider_) {
    router_.register_screen(main_menu_screen_);
    router_.register_screen(jog_screen_);
    router_.register_screen(files_screen_);
    router_.navigate_to(NavTab::Home);
}

void PortableCncApp::run() {
    run_startup_sequence();
    render_current_screen(true);

    while (true) {
        web_server_.poll();

        if (controller_.poll_storage()) {
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
    controller_.begin_calibration();
    calibration_app_.ensure_calibration(calibration);
    controller_.complete_calibration();
    storage_service_.initialize(job_state_machine_);
    web_server_.init();

    // Resolve the initial SD mount state before the first UI paint so startup
    // doesn't flash MNT and immediately redraw to OK/ERR.
    while (storage_service_.state() == StorageState::Mounting) {
        web_server_.poll();
        controller_.poll_storage();
        sleep_ms(UI_POLL_MS);
    }
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

    if (result.refresh_screen) {
        render_current_screen(false);
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
        return;
    }

    if (router_.current().tab() == NavTab::Home) {
        render_current_screen(false);
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
