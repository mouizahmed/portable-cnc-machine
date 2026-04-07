#pragma once

#include <cstddef>

#include "ui/components/ui_shell_types.h"

class StatusLineFormatter {
public:
    void format_top_bar(const StatusSnapshot& status, char* out, std::size_t size) const;
};
