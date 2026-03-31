#include "app/portable_cnc_app.h"

#include "config.h"
#include "pico/stdlib.h"

PortableCncApp::PortableCncApp(Ili9488& display, Xpt2046& touch, CalibrationStorage& storage)
    : touch_(touch),
      status_provider_(machine_state_machine_, job_state_machine_),
      calibration_app_(display, touch, storage),
      frame_(display),
      main_menu_screen_(display, frame_),
      files_screen_(display, frame_, job_state_machine_) {
    router_.register_screen(main_menu_screen_);
    router_.register_screen(files_screen_);
    router_.navigate_to(NavTab::Home);
}

void PortableCncApp::run() {
    run_startup_sequence();
    render_current_screen();

    while (true) {
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
}

bool PortableCncApp::poll_event(UiEvent& event) {
    TouchPoint point{};
    const bool touched = touch_.read_touch(point);
    if (touched && !touch_latched_) {
        touch_latched_ = true;
        event = UiEvent{UiEventType::TouchPressed, point};
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

    if (!result.has_navigation) {
        return;
    }

    if (router_.can_navigate_to(result.navigation_target) &&
        router_.navigate_to(result.navigation_target)) {
        render_current_screen();
    }
}

void PortableCncApp::render_current_screen() {
    router_.current().render(status_provider_.current());
}
