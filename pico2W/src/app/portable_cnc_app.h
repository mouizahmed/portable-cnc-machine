#pragma once

#include "app/jog/jog_state_machine.h"
#include "app/job/job_state_machine.h"
#include "app/machine/machine_state_machine.h"
#include "app/navigation/screen_router.h"
#include "app/status/status_provider.h"
#include "app/storage/storage_service.h"
#include "calibration/calibration_storage.h"
#include "calibration/touch_calibration_app.h"
#include "drivers/sd_spi_card.h"
#include "services/portable_cnc_controller.h"
#include "ui/components/app_frame.h"
#include "ui/screens/files_screen.h"
#include "ui/screens/jog_screen.h"
#include "ui/screens/main_menu_screen.h"
#include "ui/screens/screen.h"
#include "web/web_server.h"

class PortableCncApp {
public:
    PortableCncApp(Ili9488& display, Xpt2046& touch, SdSpiCard& sd_card, CalibrationStorage& storage);

    void run();

private:
    Xpt2046& touch_;
    bool touch_latched_ = false;
    TouchPoint last_touch_point_{};
    ScreenRouter router_;
    MachineStateMachine machine_state_machine_;
    JogStateMachine jog_state_machine_;
    JobStateMachine job_state_machine_;
    StorageService storage_service_;
    PortableCncController controller_;
    StatusProvider status_provider_;
    TouchCalibrationApp calibration_app_;
    AppFrame frame_;
    MainMenuScreen main_menu_screen_;
    JogScreen jog_screen_;
    FilesScreen files_screen_;
    WebServer web_server_;

    void run_startup_sequence();
    bool poll_event(UiEvent& event);
    void handle_event(const UiEvent& event);
    void render_storage_change();
    void render_current_screen(bool full = false);
};
