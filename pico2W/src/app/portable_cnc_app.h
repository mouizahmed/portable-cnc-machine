#pragma once

#include "app/comm/pico_uart_client.h"
#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/job/loaded_job_storage.h"
#include "app/machine/machine_fsm.h"
#include "app/navigation/screen_router.h"
#include "app/operations/operation_coordinator.h"
#include "app/settings/machine_settings_store.h"
#include "app/status/status_provider.h"
#include "app/storage/storage_service.h"
#include "app/worker/core1_worker.h"
#include "calibration/calibration_storage.h"
#include "calibration/touch_calibration_app.h"
#include "drivers/sd_spi_card.h"
#include "protocol/desktop_protocol.h"
#include "protocol/usb_cdc_transport.h"
#include "services/portable_cnc_controller.h"
#include "ui/components/app_frame.h"
#include "ui/screens/files_screen.h"
#include "ui/screens/jog_screen.h"
#include "ui/screens/main_menu_screen.h"
#include "ui/screens/settings_screen.h"
#include "ui/screens/screen.h"
#include "ui/screens/upload_screen.h"

class PortableCncApp {
public:
    PortableCncApp(Ili9488& display, Xpt2046& touch, SdSpiCard& sd_card, CalibrationStorage& storage);

    void run();

private:
    Ili9488& display_;
    Xpt2046& touch_;
    bool touch_latched_ = false;
    TouchPoint last_touch_point_{};
    ScreenRouter router_;
    MachineFsm machine_fsm_;
    JogStateMachine jog_state_machine_;
    JobStateMachine job_state_machine_;
    OperationCoordinator operation_coordinator_;
    Core1Worker core1_worker_;
    LoadedJobStorage loaded_job_storage_;
    MachineSettingsStore machine_settings_store_;
    StorageService storage_service_;
    PortableCncController controller_;
    PicoUartClient uart_client_;
    StatusProvider status_provider_;
    TouchCalibrationApp calibration_app_;
    AppFrame frame_;
    MainMenuScreen main_menu_screen_;
    JogScreen jog_screen_;
    FilesScreen files_screen_;
    SettingsScreen settings_screen_;
    UploadScreen upload_screen_;
    UsbCdcTransport usb_transport_;
    DesktopProtocol desktop_protocol_;
    uint32_t machine_settings_revision_ = 0;
    bool sd_was_mounted_ = false;
    bool upload_was_active_ = false;

    void run_startup_sequence();
    void show_boot_logo();
    bool poll_event(UiEvent& event);
    void handle_event(const UiEvent& event);
    void handle_ui_command(const UiEventResult& result);
    void render_storage_change();
    void render_current_screen(bool full = false);
};
