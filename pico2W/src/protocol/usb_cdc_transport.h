#pragma once
#include <cstddef>
#include <cstdarg>

// Non-blocking line-framed I/O over USB CDC stdio.
// Call poll_line() every loop iteration; it accumulates chars until '\n'.
class UsbCdcTransport {
public:
    // Returns true and fills buf (null-terminated, '\n' stripped) when a
    // complete line is ready. Returns false if no complete line yet.
    bool poll_line(char* buf, size_t max);

    // Send a null-terminated string followed by '\n'.
    void send_line(const char* text);

    // Printf-style send — appends '\n'.
    void send_fmt(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

private:
    char   buf_[512];
    size_t len_ = 0;
};
