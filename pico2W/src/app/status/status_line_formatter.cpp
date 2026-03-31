#include "app/status/status_line_formatter.h"

#include <cstdio>

void StatusLineFormatter::format_top_bar(const StatusSnapshot& status, char* out, std::size_t size) const {
    if (out == nullptr || size == 0) {
        return;
    }

    std::snprintf(out,
                  size,
                  "M:%s | SD:%s | USB:%s | T:%s | XYZ:%s | %s",
                  status.machine,
                  status.sd,
                  status.usb,
                  status.tool,
                  status.xyz,
                  status.time_text);
}
