#include "app/navigation/screen_router.h"

void ScreenRouter::register_screen(Screen& screen) {
    screens_[to_index(screen.tab())] = &screen;
}

bool ScreenRouter::can_navigate_to(NavTab tab) const {
    return screens_[to_index(tab)] != nullptr;
}

bool ScreenRouter::navigate_to(NavTab tab) {
    if (!can_navigate_to(tab)) {
        return false;
    }

    current_tab_ = tab;
    return true;
}

Screen& ScreenRouter::current() {
    Screen* screen = screens_[to_index(current_tab_)];
    if (screen != nullptr) {
        return *screen;
    }

    for (Screen* candidate : screens_) {
        if (candidate != nullptr) {
            current_tab_ = candidate->tab();
            return *candidate;
        }
    }

    // The app is expected to register at least one screen before using the router.
    return *screens_[to_index(NavTab::Home)];
}

const Screen& ScreenRouter::current() const {
    Screen* screen = screens_[to_index(current_tab_)];
    if (screen != nullptr) {
        return *screen;
    }

    for (Screen* candidate : screens_) {
        if (candidate != nullptr) {
            return *candidate;
        }
    }

    return *screens_[to_index(NavTab::Home)];
}
