#include "app/portable_cnc_app.h"

#include "config.h"
#include "pico/stdlib.h"

PortableCncApp::PortableCncApp(Ili9488& display, Xpt2046& touch, SdSpiCard& sd_card, CalibrationStorage& storage)
    : touch_(touch),
      storage_service_(sd_card),
      uart_client_(machine_state_machine_, jog_state_machine_, job_state_machine_, storage_service_),
      status_provider_(machine_state_machine_, jog_state_machine_, job_state_machine_, storage_service_),
      calibration_app_(display, touch, storage),
      frame_(display),
      main_menu_screen_(display, frame_, machine_state_machine_, job_state_machine_),
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

        if (uart_client_.poll()) {
            frame_.render_status_bar(status_provider_.current());
            render_current_screen(false);
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

    // Resolve the initial SD mount state before the first UI paint so startup
    // doesn't flash MNT and immediately redraw to OK/ERR.
    while (storage_service_.state() == StorageState::Mounting) {
        storage_service_.poll(job_state_machine_);
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

    if (result.command != UiCommandType::None) {
        handle_ui_command(result);
        result.refresh_status_bar = true;
        result.refresh_screen = true;
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

void PortableCncApp::handle_ui_command(const UiEventResult& result) {
    switch (result.command) {
        case UiCommandType::None:
            break;
        case UiCommandType::SelectFile:
            uart_client_.select_file(static_cast<int16_t>(result.selected_index));
            break;
        case UiCommandType::StartJob:
            uart_client_.upload_and_run_selected_job();
            break;
        case UiCommandType::HoldJob:
            uart_client_.hold();
            break;
        case UiCommandType::ResumeJob:
            uart_client_.resume();
            break;
        case UiCommandType::JogMove:
            uart_client_.jog(result.jog_action);
            break;
        case UiCommandType::HomeAll:
            uart_client_.home_all();
            break;
        case UiCommandType::ZeroAll:
            uart_client_.zero_all();
            break;
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
