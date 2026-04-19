#include "protocol/usb_cdc_transport.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"

UsbCdcTransport::PacketKind UsbCdcTransport::poll(char* line_buf, size_t line_max, FramePacket& frame) {
    while (true) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            return PacketKind::None;
        }

        const uint8_t byte = static_cast<uint8_t>(c);
        switch (mode_) {
            case ReceiveMode::Idle:
                if (byte == kTransferFrameMarker) {
                    reset_frame_parse();
                    mode_ = ReceiveMode::FrameHeader;
                } else if (byte == '@') {
                    line_len_ = 0;
                    line_[line_len_++] = '@';
                    mode_ = ReceiveMode::Line;
                }
                break;

            case ReceiveMode::Line:
                if (byte == '\r') {
                    break;
                }
                if (byte == '\n') {
                    if (line_len_ == 0) {
                        mode_ = ReceiveMode::Idle;
                        break;
                    }

                    const size_t copy = (line_len_ < line_max - 1) ? line_len_ : (line_max - 1);
                    std::memcpy(line_buf, line_, copy);
                    line_buf[copy] = '\0';
                    line_len_ = 0;
                    mode_ = ReceiveMode::Idle;
                    return PacketKind::Line;
                }

                if (line_len_ < sizeof(line_) - 1) {
                    line_[line_len_++] = static_cast<char>(byte);
                }
                break;

            case ReceiveMode::FrameHeader:
                frame_header_[frame_header_len_++] = byte;
                if (frame_header_len_ < kTransferFrameHeaderSize) {
                    break;
                }

                frame_payload_len_ = read_u16_le(frame_header_ + 7);
                if (frame_payload_len_ > kMaxTransferPayloadSize) {
                    reset_receive_state();
                    break;
                }

                frame_expected_body_len_ = frame_payload_len_ + sizeof(uint32_t);
                frame_body_len_ = 0;
                mode_ = ReceiveMode::FrameBody;
                break;

            case ReceiveMode::FrameBody:
                if (frame_body_len_ >= sizeof(frame_body_)) {
                    reset_receive_state();
                    break;
                }

                frame_body_[frame_body_len_++] = byte;
                if (frame_body_len_ < frame_expected_body_len_) {
                    break;
                }

                {
                    const uint32_t expected_crc = read_u32_le(frame_body_ + frame_payload_len_);
                    uint8_t crc_buffer[kTransferFrameHeaderSize + kMaxTransferPayloadSize]{};
                    std::memcpy(crc_buffer, frame_header_, kTransferFrameHeaderSize);
                    if (frame_payload_len_ > 0) {
                        std::memcpy(crc_buffer + kTransferFrameHeaderSize, frame_body_, frame_payload_len_);
                    }

                    if (crc32(crc_buffer, kTransferFrameHeaderSize + frame_payload_len_) == expected_crc) {
                        frame.type = frame_header_[0];
                        frame.transfer_id = frame_header_[1];
                        frame.flags = frame_header_[2];
                        frame.seq = read_u32_le(frame_header_ + 3);
                        frame.payload_len = frame_payload_len_;
                        if (frame_payload_len_ > 0) {
                            std::memcpy(frame.payload, frame_body_, frame_payload_len_);
                        }
                        reset_receive_state();
                        return PacketKind::Frame;
                    }
                }

                reset_receive_state();
                break;
        }
    }
}

void UsbCdcTransport::send_line(const char* text) {
    std::puts(text);
    std::fflush(stdout);
}

void UsbCdcTransport::send_fmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::putchar('\n');
    std::fflush(stdout);
}

void UsbCdcTransport::send_frame(uint8_t type, uint8_t transfer_id, uint32_t seq,
                                 const uint8_t* payload, uint16_t payload_len) {
    if (payload_len > kMaxTransferPayloadSize) {
        return;
    }

    uint8_t buffer[1 + kTransferFrameHeaderSize + kMaxTransferPayloadSize + sizeof(uint32_t)]{};
    size_t offset = 0;
    buffer[offset++] = kTransferFrameMarker;
    buffer[offset++] = type;
    buffer[offset++] = transfer_id;
    buffer[offset++] = 0;
    write_u32_le(buffer + offset, seq);
    offset += sizeof(uint32_t);
    write_u16_le(buffer + offset, payload_len);
    offset += sizeof(uint16_t);
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(buffer + offset, payload, payload_len);
        offset += payload_len;
    }
    const uint32_t crc = crc32(buffer + 1, kTransferFrameHeaderSize + payload_len);
    write_u32_le(buffer + offset, crc);
    offset += sizeof(uint32_t);

    std::fwrite(buffer, 1, offset, stdout);
    std::fflush(stdout);
}

void UsbCdcTransport::write_u16_le(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void UsbCdcTransport::write_u32_le(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint16_t UsbCdcTransport::read_u16_le(const uint8_t* in) {
    return static_cast<uint16_t>(in[0] | (static_cast<uint16_t>(in[1]) << 8));
}

uint32_t UsbCdcTransport::read_u32_le(const uint8_t* in) {
    return static_cast<uint32_t>(in[0])
         | (static_cast<uint32_t>(in[1]) << 8)
         | (static_cast<uint32_t>(in[2]) << 16)
         | (static_cast<uint32_t>(in[3]) << 24);
}

uint32_t UsbCdcTransport::crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return ~crc;
}

void UsbCdcTransport::reset_frame_parse() {
    frame_header_len_ = 0;
    frame_body_len_ = 0;
    frame_expected_body_len_ = 0;
    frame_payload_len_ = 0;
}

void UsbCdcTransport::reset_receive_state() {
    mode_ = ReceiveMode::Idle;
    line_len_ = 0;
    reset_frame_parse();
}
