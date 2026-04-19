#pragma once

#include <cstddef>
#include <cstdint>

enum class JogAction : uint8_t {
    MoveXNegative,
    MoveXPositive,
    MoveYNegative,
    MoveYPositive,
    MoveZNegative,
    MoveZPositive,
    SelectStepFine,
    SelectStepMedium,
    SelectStepCoarse,
    SelectFeedSlow,
    SelectFeedNormal,
    SelectFeedFast,
    ZeroAll,
    HomeAll,
};

class JogStateMachine {
public:
    bool handle_action(JogAction action);

    float x() const;
    float y() const;
    float z() const;
    uint8_t step_index() const;
    uint8_t feed_index() const;
    float step_size_mm() const;
    int16_t feed_rate_mm_min() const;
    const char* step_label() const;
    const char* feed_label() const;

    void format_xyz(char* out, std::size_t size) const;
    void format_axis_line(char axis, char* out, std::size_t size) const;
    void set_position(float x, float y, float z);

private:
    static constexpr float kStepSizes[3]{0.1f, 1.0f, 10.0f};
    static constexpr const char* kStepLabels[3]{"0.1 MM", "1.0 MM", "10 MM"};
    static constexpr int16_t kFeedRates[3]{100, 500, 1200};
    static constexpr const char* kFeedLabels[3]{"SLOW", "MED", "FAST"};

    float x_ = 0.0f;
    float y_ = 0.0f;
    float z_ = 0.0f;
    uint8_t step_index_ = 1;
    uint8_t feed_index_ = 1;

    void move_axis(float& axis, float direction);
};
