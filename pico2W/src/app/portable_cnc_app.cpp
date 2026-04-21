#include "app/portable_cnc_app.h"

#include "config.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "ui/assets/boot_logo_rgb565.h"

namespace {
// Dev shortcut: skip the blocking touch calibration workflow during firmware iteration.
// Keep false for normal persisted calibration / calibration UI behavior.
constexpr bool kSkipTouchCalibrationForDev = false;
constexpr uint32_t kBootLogoHoldMs = 1200;

void release_shared_spi_devices() {
    gpio_put(PIN_LCD_CS, 1);
    gpio_put(PIN_TOUCH_CS, 1);
    gpio_put(PIN_SD_CS, 1);
}
}

PortableCncApp::PortableCncApp(Ili9488& display, Xpt2046& touch, SdSpiCard& sd_card, CalibrationStorage& storage)
    : display_(display),
      touch_(touch),
      storage_service_(sd_card, core1_worker_),
      controller_(machine_fsm_,
                  jog_state_machine_,
                  job_state_machine_,
                  machine_settings_store_,
                  storage_service_,
                  operation_coordinator_,
                  core1_worker_),
      uart_client_(machine_fsm_, jog_state_machine_, job_state_machine_, storage_service_),
      status_provider_(machine_fsm_, jog_state_machine_, job_state_machine_, storage_service_),
      calibration_app_(display, touch, storage),
      frame_(display),
      main_menu_screen_(display, frame_, controller_),
      jog_screen_(display, frame_, controller_),
      files_screen_(display, frame_, controller_),
      settings_screen_(display, frame_, controller_),
      upload_screen_(display),
      desktop_protocol_(usb_transport_,
                        machine_fsm_,
                        jog_state_machine_,
                        job_state_machine_,
                        loaded_job_storage_,
                        machine_settings_store_,
                        storage_service_,
                        sd_card,
                        operation_coordinator_,
                        core1_worker_) {
    files_screen_.bind_protocol(desktop_protocol_);
    router_.register_screen(main_menu_screen_);
    router_.register_screen(jog_screen_);
    router_.register_screen(files_screen_);
    router_.register_screen(settings_screen_);
    router_.navigate_to(NavTab::Home);
}

void PortableCncApp::run() {
    run_startup_sequence();
    sd_was_mounted_ = storage_service_.is_mounted();
    machine_settings_revision_ = machine_settings_store_.revision();
    desktop_protocol_.consume_state_changed();
    render_current_screen(true);

    while (true) {
        const bool upload_active_now = desktop_protocol_.upload_active();
        const bool upload_just_ended = upload_was_active_ && !upload_active_now;
        if (!upload_active_now && !upload_just_ended && controller_.poll_storage()) {
            render_storage_change();
        }
        upload_was_active_ = upload_active_now;

        const bool sd_mounted_now = storage_service_.is_mounted();
        if (sd_mounted_now && !sd_was_mounted_) {
            sd_was_mounted_ = true;
            desktop_protocol_.on_sd_mounted();
            desktop_protocol_.restore_persisted_job();
        } else if (!sd_mounted_now && sd_was_mounted_) {
            sd_was_mounted_ = false;
            desktop_protocol_.on_sd_removed();
        }

        desktop_protocol_.poll();

        const uint32_t machine_settings_revision_now = machine_settings_store_.revision();
        if (machine_settings_revision_now != machine_settings_revision_) {
            machine_settings_revision_ = machine_settings_revision_now;
            desktop_protocol_.emit_machine_settings();
            if (!desktop_protocol_.upload_active() &&
                router_.current().tab() == NavTab::Settings) {
                render_current_screen(false);
            }
        }

        const bool upload_ended_this_tick =
            upload_active_now && !desktop_protocol_.upload_active();

        // Progress bar update removed — desktop app shows progress.
        // The upload screen (with abort button) is drawn once when upload starts.

        bool needs_render = desktop_protocol_.consume_state_changed();
        if (desktop_protocol_.consume_file_list_changed()) {
            controller_.refresh_job_files();
            if (router_.current().tab() == NavTab::Files) {
                needs_render = true;
            }
        }
        if (needs_render) {
            render_current_screen(upload_ended_this_tick);
        }

        const PicoUartPollResult uart_result = uart_client_.poll();
        if (uart_result.link_transition == MotionLinkTransition::Connected) {
            desktop_protocol_.emit_event("TEENSY_CONNECTED");
        } else if (uart_result.link_transition == MotionLinkTransition::Disconnected) {
            desktop_protocol_.emit_event("TEENSY_DISCONNECTED");
        }

        if (uart_result.machine_changed) {
            desktop_protocol_.emit_state_update();
        }
        if (uart_result.job_changed) {
            desktop_protocol_.emit_job();
        }
        if (uart_result.position_changed) {
            desktop_protocol_.emit_position();
        }

        if (uart_result.machine_changed || uart_result.job_changed || uart_result.position_changed) {
            frame_.render_status_bar(status_provider_.current());
            if (!desktop_protocol_.upload_active()) {
                render_current_screen(false);
                if (uart_result.machine_changed) {
                    desktop_protocol_.consume_state_changed();
                }
            }
        }

        UiEvent event{};
        if (poll_event(event)) {
            handle_event(event);
        }

        if (desktop_protocol_.storage_transfer_active()) {
            sleep_us(TRANSFER_POLL_US);
        } else {
            sleep_ms(UI_POLL_MS);
        }
    }
}

void PortableCncApp::run_startup_sequence() {
    show_boot_logo();
    release_shared_spi_devices();

    if (!kSkipTouchCalibrationForDev) {
        TouchCalibration calibration{};
        controller_.begin_calibration();
        StorageTransferStateMachine idle_transfer{};
        const RequestDecision calibration_decision = operation_coordinator_.decide(
            OperationRequest{OperationRequestType::CalibrationSave, OperationRequestSource::System},
            machine_fsm_,
            idle_transfer,
            JobStreamState::Idle,
            core1_worker_.snapshot(),
            storage_service_.state());
        if (calibration_decision.type == RequestDecisionType::AcceptNow ||
            calibration_decision.type == RequestDecisionType::PreemptAndAccept ||
            calibration_decision.type == RequestDecisionType::AbortCurrentAndAccept ||
            calibration_decision.type == RequestDecisionType::SuppressBackgroundAndAccept) {
            calibration_app_.ensure_calibration(calibration);
        }
        controller_.complete_calibration();
        touch_latched_ = false;
        last_touch_point_ = TouchPoint{};
        release_shared_spi_devices();
    }

    release_shared_spi_devices();
    core1_worker_.start();
    storage_service_.initialize(job_state_machine_);

    while (storage_service_.state() == StorageState::Mounting) {
        controller_.poll_storage();
        sleep_ms(UI_POLL_MS);
    }

    machine_fsm_.handle_event(MachineEvent::BootTimeout);
    if (storage_service_.is_mounted()) {
        desktop_protocol_.on_sd_mounted();
        desktop_protocol_.restore_persisted_job();
    } else {
        desktop_protocol_.emit_state_update();
    }
}

void PortableCncApp::show_boot_logo() {
    display_.fill_screen(COLOR_BG);

    const int16_t logo_x = static_cast<int16_t>((LCD_WIDTH - kBootLogoWidth) / 2);
    const int16_t logo_y = static_cast<int16_t>((LCD_HEIGHT - kBootLogoHeight) / 2) - 12;
    display_.draw_rgb565_bitmap(logo_x, logo_y, kBootLogoWidth, kBootLogoHeight, kBootLogoPixels);

    constexpr char kTitle[] = "Portable CNC";
    constexpr char kSubtitle[] = "Control";
    const int16_t title_x = static_cast<int16_t>((LCD_WIDTH - display_.text_width(kTitle, 2)) / 2);
    const int16_t subtitle_x = static_cast<int16_t>((LCD_WIDTH - display_.text_width(kSubtitle, 2)) / 2);
    const int16_t text_y = static_cast<int16_t>(logo_y + kBootLogoHeight + 14);

    display_.draw_text(title_x, text_y, kTitle, COLOR_TEXT, COLOR_BG, 2);
    display_.draw_text(subtitle_x, static_cast<int16_t>(text_y + 18), kSubtitle, COLOR_MUTED, COLOR_BG, 2);

    sleep_ms(kBootLogoHoldMs);
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
    if (desktop_protocol_.upload_active()) {
        if (event.type == UiEventType::TouchPressed &&
            upload_screen_.hit_test_abort(event.touch)) {
            desktop_protocol_.abort_upload();
            render_current_screen(true);
        }
        return;
    }

    UiEventResult result = frame_.handle_event(event);
    if (!result.handled) {
        result = router_.current().handle_event(event);
    }

    if (result.command != UiCommandType::None) {
        handle_ui_command(result);
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
        if (result.navigation_target == NavTab::Files) {
            controller_.refresh_job_files();
        }
        render_current_screen(false);
    }
}

void PortableCncApp::handle_ui_command(const UiEventResult& result) {
    switch (result.command) {
        case UiCommandType::None:
            break;
        case UiCommandType::SelectFile:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::FileLoad)) {
                break;
            }
            uart_client_.select_file(static_cast<int16_t>(result.selected_index));
            break;
        case UiCommandType::StartJob:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::JobStart)) {
                break;
            }
            uart_client_.upload_and_run_loaded_job();
            break;
        case UiCommandType::HoldJob:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::JobHold)) {
                break;
            }
            uart_client_.hold();
            break;
        case UiCommandType::ResumeJob:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::JobResume)) {
                break;
            }
            uart_client_.resume();
            break;
        case UiCommandType::JogMove:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::Jog)) {
                break;
            }
            uart_client_.jog(result.jog_action);
            break;
        case UiCommandType::HomeAll:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::HomeAll)) {
                break;
            }
            uart_client_.home_all();
            break;
        case UiCommandType::ZeroAll:
            if (!desktop_protocol_.allow_ui_operation(OperationRequestType::ZeroAll)) {
                break;
            }
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
    if (desktop_protocol_.upload_active()) {
        upload_screen_.render(desktop_protocol_.upload_name());
        return;
    }

    if (full) {
        router_.current().render(status_provider_.current());
        return;
    }

    frame_.render_status_bar(status_provider_.current());
    frame_.render_nav_bar(router_.current().tab());
    frame_.clear_content();
    router_.current().render_content();
}
