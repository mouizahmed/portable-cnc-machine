#include "protocol/usb_cdc_transport.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "pico/stdlib.h"

bool UsbCdcTransport::poll_line(char* out, size_t max) {
    while (true) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return false;

        char ch = static_cast<char>(c);
        if (ch == '\r') continue;

        if (ch == '\n') {
            if (len_ == 0) continue;  // skip blank lines
            size_t copy = (len_ < max - 1) ? len_ : (max - 1);
            std::memcpy(out, buf_, copy);
            out[copy] = '\0';
            len_ = 0;
            return true;
        }

        if (len_ < sizeof(buf_) - 1) {
            buf_[len_++] = ch;
        }
        // overflow: silently drop (line too long)
    }
}

void UsbCdcTransport::send_line(const char* text) {
    std::puts(text);
}

void UsbCdcTransport::send_fmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::putchar('\n');
}
