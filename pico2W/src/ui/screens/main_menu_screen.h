#pragma once

#include <array>
#include <cstdint>

#include "app/job/job_state_machine.h"
#include "app/machine/machine_state_machine.h"
#include "ui/components/app_frame.h"
#include "ui/components/menu_card.h"
#include "ui/screens/screen.h"

class MainMenuScreen : public Screen {
public:
    MainMenuScreen(Ili9488& display,
                   AppFrame& frame,
                   MachineStateMachine& machine_state_machine,
                   JobStateMachine& job_state_machine);

    NavTab tab() const override;
    void render(const StatusSnapshot& status) override;
    void render_content() override;
    UiEventResult handle_event(const UiEvent& event) override;

private:
    enum class HomeAction : uint8_t {
        Primary,
        Jog,
        SetZero,
        ToolSetup,
    };

    struct ResolvedCard {
        MenuCardSpec spec;
        HomeAction action;
    };

    AppFrame& frame_;
    MachineStateMachine& machine_state_machine_;
    JobStateMachine& job_state_machine_;
    MenuCard menu_card_;

    std::array<ResolvedCard, 4> build_cards() const;
};
