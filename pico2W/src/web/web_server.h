#pragma once

#include "app/status/status_provider.h"
#include "services/portable_cnc_controller.h"

class WebServer {
public:
    WebServer(PortableCncController& controller, StatusProvider& status_provider);

    bool init();
    void poll();
    PortableCncController& controller();
    StatusProvider& status_provider();

private:
    PortableCncController& controller_;
    StatusProvider& status_provider_;
    bool initialized_ = false;
};
