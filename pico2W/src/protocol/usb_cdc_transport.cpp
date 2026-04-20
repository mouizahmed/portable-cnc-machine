#include "protocol/usb_cdc_transport.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"

static uint32_t crc32_update_table(uint32_t crc, const uint8_t* data, size_t len) {
    static bool initialized = false;
    static uint32_t table[256];

    if (!initialized) {
        for (uint32_t value = 0; value < 256; value++) {
            uint32_t entry = value;
            for (int bit = 0; bit < 8; bit++) {
                entry = (entry & 1u) != 0 ? (entry >> 1) ^ 0xEDB88320u : (entry >> 1);
            }
            table[value] = entry;
        }
        initialized = true;
    }

    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

UsbCdcTransport::PacketKind UsbCdcTransport::poll(char* line_buf, size_t line_max, FramePacket& frame) {
    while (true) {
        uint8_t byte = 0;
        if (!read_next_byte(byte)) {
            return PacketKind::None;
        }

        switch (mode_) {
            case ReceiveMode::Idle:
                if (byte == kTransferFrameMarker) {
                    reset_frame_parse();
                    mode_ = ReceiveMode::FramePacket;
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

            case ReceiveMode::FramePacket:
                if (byte != kTransferFrameMarker) {
                    if (byte == kTransferFrameEscape) {
                        mode_ = ReceiveMode::FrameEscape;
                        break;
                    }
                    append_frame_packet_byte(byte);
                    break;
                }

                if (frame_packet_len_ == 0) {
                    reset_frame_parse();
                    break;
                }

                {
                    const int decoded_len = cobs_decode(frame_packet_,
                                                        frame_packet_len_,
                                                        decoded_frame_,
                                                        sizeof(decoded_frame_));
                    if (decoded_len < static_cast<int>(kTransferFrameHeaderSize + sizeof(uint32_t))) {
                        reset_receive_state();
                        break;
                    }

                    const uint16_t payload_len = read_u16_le(decoded_frame_ + 7);
                    const size_t expected_len = kTransferFrameHeaderSize + payload_len + sizeof(uint32_t);
                    if (payload_len > kMaxTransferPayloadSize ||
                        static_cast<size_t>(decoded_len) != expected_len) {
                        reset_receive_state();
                        break;
                    }

                    const uint32_t expected_crc = read_u32_le(decoded_frame_ + kTransferFrameHeaderSize + payload_len);
                    if (crc32(decoded_frame_, kTransferFrameHeaderSize + payload_len) == expected_crc) {
                        frame.type = decoded_frame_[0];
                        frame.transfer_id = decoded_frame_[1];
                        frame.flags = decoded_frame_[2];
                        frame.seq = read_u32_le(decoded_frame_ + 3);
                        frame.payload_len = payload_len;
                        if (payload_len > 0) {
                            std::memcpy(frame.payload, decoded_frame_ + kTransferFrameHeaderSize, payload_len);
                        }
                        reset_receive_state();
                        return PacketKind::Frame;
                    }
                }

                reset_receive_state();
                break;

            case ReceiveMode::FrameEscape:
                append_frame_packet_byte(static_cast<uint8_t>(byte ^ kTransferFrameEscapeXor));
                if (mode_ == ReceiveMode::FrameEscape) {
                    mode_ = ReceiveMode::FramePacket;
                }
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

    size_t offset = 0;
    tx_raw_[offset++] = type;
    tx_raw_[offset++] = transfer_id;
    tx_raw_[offset++] = 0;
    write_u32_le(tx_raw_ + offset, seq);
    offset += sizeof(uint32_t);
    write_u16_le(tx_raw_ + offset, payload_len);
    offset += sizeof(uint16_t);
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(tx_raw_ + offset, payload, payload_len);
        offset += payload_len;
    }
    const uint32_t crc = crc32(tx_raw_, kTransferFrameHeaderSize + payload_len);
    write_u32_le(tx_raw_ + offset, crc);
    offset += sizeof(uint32_t);

    const size_t encoded_len = cobs_encode(tx_raw_, offset, tx_encoded_, sizeof(tx_encoded_));
    if (encoded_len == 0) {
        return;
    }

    size_t wire_len = 0;
    tx_wire_[wire_len++] = kTransferFrameMarker;
    for (size_t i = 0; i < encoded_len; ++i) {
        if (tx_encoded_[i] == kTransferFrameMarker ||
            tx_encoded_[i] == kTransferFrameEscape ||
            tx_encoded_[i] == '\r' ||
            tx_encoded_[i] == '\n') {
            tx_wire_[wire_len++] = kTransferFrameEscape;
            tx_wire_[wire_len++] = static_cast<uint8_t>(tx_encoded_[i] ^ kTransferFrameEscapeXor);
            continue;
        }
        tx_wire_[wire_len++] = tx_encoded_[i];
    }
    tx_wire_[wire_len++] = kTransferFrameMarker;

    std::fwrite(tx_wire_, 1, wire_len, stdout);
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
    return ~crc32_update_table(0xFFFFFFFFu, data, len);
}

size_t UsbCdcTransport::cobs_encode(const uint8_t* input, size_t input_len, uint8_t* output, size_t output_cap) {
    if (input == nullptr || output == nullptr || output_cap == 0) {
        return 0;
    }

    size_t read = 0;
    size_t write = 1;
    size_t code_index = 0;
    uint8_t code = 1;

    while (read < input_len) {
        if (write >= output_cap) {
            return 0;
        }

        if (input[read] == 0) {
            output[code_index] = code;
            code = 1;
            code_index = write++;
            read++;
            continue;
        }

        output[write++] = input[read++];
        code++;
        if (code == 0xFF) {
            output[code_index] = code;
            code = 1;
            code_index = write++;
        }
    }

    if (code_index >= output_cap) {
        return 0;
    }
    output[code_index] = code;
    return write;
}

int UsbCdcTransport::cobs_decode(const uint8_t* input, size_t input_len, uint8_t* output, size_t output_cap) {
    if (input == nullptr || output == nullptr) {
        return -1;
    }

    size_t read = 0;
    size_t write = 0;
    while (read < input_len) {
        const uint8_t code = input[read++];
        if (code == 0) {
            return -1;
        }

        const size_t copy = static_cast<size_t>(code - 1);
        if (read + copy > input_len || write + copy > output_cap) {
            return -1;
        }

        if (copy > 0) {
            std::memcpy(output + write, input + read, copy);
        }
        read += copy;
        write += copy;

        if (code < 0xFF && read < input_len) {
            if (write >= output_cap) {
                return -1;
            }
            output[write++] = 0;
        }
    }

    return static_cast<int>(write);
}

bool UsbCdcTransport::read_next_byte(uint8_t& byte) {
    if (rx_pos_ >= rx_len_) {
        const int read = stdio_get_until(reinterpret_cast<char*>(rx_buffer_),
                                         static_cast<int>(sizeof(rx_buffer_)),
                                         make_timeout_time_us(0));
        if (read <= 0) {
            rx_len_ = 0;
            rx_pos_ = 0;
            return false;
        }

        rx_len_ = static_cast<size_t>(read);
        rx_pos_ = 0;
    }

    byte = rx_buffer_[rx_pos_++];
    return true;
}

bool UsbCdcTransport::append_frame_packet_byte(uint8_t byte) {
    if (frame_packet_len_ >= sizeof(frame_packet_)) {
        reset_receive_state();
        return false;
    }

    frame_packet_[frame_packet_len_++] = byte;
    return true;
}

void UsbCdcTransport::reset_frame_parse() {
    frame_packet_len_ = 0;
}

void UsbCdcTransport::reset_receive_state() {
    mode_ = ReceiveMode::Idle;
    line_len_ = 0;
    reset_frame_parse();
}
