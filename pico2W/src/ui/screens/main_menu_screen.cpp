#include "ui/screens/main_menu_screen.h"

#include "ui/layout/ui_layout.h"

namespace {
MenuCardSpec make_card(std::size_t index,
                       const char* title,
                       const char* subtitle,
                       NavTab target_tab,
                       bool enabled) {
    return MenuCardSpec{
        UiLayout::main_menu_card_rect(index),
        title,
        subtitle,
        target_tab,
        enabled,
    };
}
}  // namespace

MainMenuScreen::MainMenuScreen(Ili9488& display,
                               AppFrame& frame,
                               MachineStateMachine& machine_state_machine,
                               JobStateMachine& job_state_machine)
    : frame_(frame),
      machine_state_machine_(machine_state_machine),
      job_state_machine_(job_state_machine),
      menu_card_(display) {}

NavTab MainMenuScreen::tab() const {
    return NavTab::Home;
}

void MainMenuScreen::render(const StatusSnapshot& status) {
    frame_.render_chrome(status, NavTab::Home);
    render_content();
}

void MainMenuScreen::render_content() {
    const std::array<ResolvedCard, 4> cards = build_cards();
    for (const ResolvedCard& card : cards) {
        menu_card_.render(card.spec);
    }
}

UiEventResult MainMenuScreen::handle_event(const UiEvent& event) {
    if (event.type != UiEventType::TouchPressed) {
        return UiEventResult{};
    }

    const std::array<ResolvedCard, 4> cards = build_cards();
    for (const ResolvedCard& card : cards) {
        if (!ui_rect_contains(card.spec.rect, event.touch)) {
            continue;
        }

        if (!card.spec.enabled) {
            return UiEventResult{true, false, tab()};
        }

        switch (card.action) {
            case HomeAction::Primary:
                if (!job_state_machine_.has_selection()) {
                    return UiEventResult{true, true, NavTab::Files};
                }

                if (machine_state_machine_.state() == MachineState::Idle) {
                    const bool started_job = job_state_machine_.handle_event(JobEvent::StartRun);
                    const bool started_machine = machine_state_machine_.handle_event(MachineEvent::RunRequested);
                    return UiEventResult{true, false, tab(), started_job || started_machine, started_job || started_machine};
                }

                if (machine_state_machine_.state() == MachineState::Running) {
                    const bool changed = machine_state_machine_.handle_event(MachineEvent::HoldRequested);
                    return UiEventResult{true, false, tab(), changed, changed};
                }

                if (machine_state_machine_.state() == MachineState::Hold) {
                    const bool changed = machine_state_machine_.handle_event(MachineEvent::RunRequested);
                    return UiEventResult{true, false, tab(), changed, changed};
                }
                return UiEventResult{true, false, tab()};
            case HomeAction::Jog:
                return UiEventResult{true, true, NavTab::Jog};
            case HomeAction::SetZero:
            case HomeAction::ToolSetup:
                return UiEventResult{true, false, tab()};
        }
    }

    return UiEventResult{};
}

std::array<MainMenuScreen::ResolvedCard, 4> MainMenuScreen::build_cards() const {
    const MachineState machine_state = machine_state_machine_.state();
    const bool has_job = job_state_machine_.has_selection();

    MenuCardSpec primary = make_card(0, "LOAD JOB", "PICK G-CODE", NavTab::Files, true);
    MenuCardSpec jog = make_card(1, "JOG", "MOVE AXES", NavTab::Jog, true);
    MenuCardSpec set_zero = make_card(2, "SET ZERO", "WORK OFFSET", NavTab::Home, false);
    MenuCardSpec tool_setup = make_card(3, "TOOL SETUP", "TOUCH-OFF / CHANGE", NavTab::Home, false);

    switch (machine_state) {
        case MachineState::Running:
            primary = make_card(0, "PAUSE JOB", "HOLD MOTION", NavTab::Home, true);
            jog = make_card(1, "JOG", "DISABLED", NavTab::Home, false);
            set_zero = make_card(2, "SET ZERO", "LOCKED", NavTab::Home, false);
            tool_setup = make_card(3, "TOOL SETUP", "LOCKED", NavTab::Home, false);
            break;
        case MachineState::Hold:
            primary = make_card(0, "RESUME JOB", "CONTINUE RUN", NavTab::Home, true);
            jog = make_card(1, "JOG", "DISABLED", NavTab::Home, false);
            set_zero = make_card(2, "SET ZERO", "LOCKED", NavTab::Home, false);
            tool_setup = make_card(3, "TOOL SETUP", "LOCKED", NavTab::Home, false);
            break;
        case MachineState::Alarm:
        case MachineState::Estop:
            primary = make_card(0, "ALARM", "CHECK MACHINE", NavTab::Home, false);
            jog = make_card(1, "JOG", "DISABLED", NavTab::Home, false);
            set_zero = make_card(2, "SET ZERO", "DISABLED", NavTab::Home, false);
            tool_setup = make_card(3, "TOOL SETUP", "DISABLED", NavTab::Home, false);
            break;
        case MachineState::Idle:
            if (has_job) {
                primary = make_card(0, "START JOB", "FILE READY", NavTab::Home, true);
                jog = make_card(1, "JOG", "FINAL POSITION", NavTab::Jog, true);
                set_zero = make_card(2, "SET ZERO", "VERIFY OFFSET", NavTab::Home, false);
                tool_setup = make_card(3, "TOOL SETUP", "CONFIRM TOOL", NavTab::Home, false);
            }
            break;
        case MachineState::Booting:
        case MachineState::Calibrating:
            primary = make_card(0, "SYSTEM", "PLEASE WAIT", NavTab::Home, false);
            jog = make_card(1, "JOG", "DISABLED", NavTab::Home, false);
            set_zero = make_card(2, "SET ZERO", "DISABLED", NavTab::Home, false);
            tool_setup = make_card(3, "TOOL SETUP", "DISABLED", NavTab::Home, false);
            break;
    }

    return {{
        {primary, HomeAction::Primary},
        {jog, HomeAction::Jog},
        {set_zero, HomeAction::SetZero},
        {tool_setup, HomeAction::ToolSetup},
    }};
}
