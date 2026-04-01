#include "app/jog/jog_state_machine.h"

#include <cstdio>

bool JogStateMachine::handle_action(JogAction action) {
    switch (action) {
        case JogAction::MoveXNegative:
            move_axis(x_, -1.0f);
            return true;
        case JogAction::MoveXPositive:
            move_axis(x_, 1.0f);
            return true;
        case JogAction::MoveYNegative:
            move_axis(y_, -1.0f);
            return true;
        case JogAction::MoveYPositive:
            move_axis(y_, 1.0f);
            return true;
        case JogAction::MoveZNegative:
            move_axis(z_, -1.0f);
            return true;
        case JogAction::MoveZPositive:
            move_axis(z_, 1.0f);
            return true;
        case JogAction::SelectStepFine:
            step_index_ = 0;
            return true;
        case JogAction::SelectStepMedium:
            step_index_ = 1;
            return true;
        case JogAction::SelectStepCoarse:
            step_index_ = 2;
            return true;
        case JogAction::SelectFeedSlow:
            feed_index_ = 0;
            return true;
        case JogAction::SelectFeedNormal:
            feed_index_ = 1;
            return true;
        case JogAction::SelectFeedFast:
            feed_index_ = 2;
            return true;
        case JogAction::ZeroAll:
        case JogAction::HomeAll:
            x_ = 0.0f;
            y_ = 0.0f;
            z_ = 0.0f;
            return true;
    }

    return false;
}

float JogStateMachine::x() const {
    return x_;
}

float JogStateMachine::y() const {
    return y_;
}

float JogStateMachine::z() const {
    return z_;
}

uint8_t JogStateMachine::step_index() const {
    return step_index_;
}

uint8_t JogStateMachine::feed_index() const {
    return feed_index_;
}

float JogStateMachine::step_size_mm() const {
    return kStepSizes[step_index_];
}

int16_t JogStateMachine::feed_rate_mm_min() const {
    return kFeedRates[feed_index_];
}

const char* JogStateMachine::step_label() const {
    return kStepLabels[step_index_];
}

const char* JogStateMachine::feed_label() const {
    return kFeedLabels[feed_index_];
}

void JogStateMachine::format_xyz(char* out, std::size_t size) const {
    if (out == nullptr || size == 0) {
        return;
    }

    std::snprintf(out, size, "%.1f,%.1f,%.1f", x_, y_, z_);
}

void JogStateMachine::format_axis_line(char axis, char* out, std::size_t size) const {
    if (out == nullptr || size == 0) {
        return;
    }

    float value = 0.0f;
    switch (axis) {
        case 'X':
            value = x_;
            break;
        case 'Y':
            value = y_;
            break;
        case 'Z':
            value = z_;
            break;
        default:
            break;
    }

    std::snprintf(out, size, "%c %+.1f", axis, value);
}

void JogStateMachine::move_axis(float& axis, float direction) {
    axis += direction * step_size_mm();
}
