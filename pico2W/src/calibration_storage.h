#pragma once

#include "xpt2046.h"

class CalibrationStorage {
public:
    bool load(TouchCalibration& calibration) const;
    bool save(const TouchCalibration& calibration) const;

    static bool has_reasonable_ranges(const TouchCalibration& calibration);
};
