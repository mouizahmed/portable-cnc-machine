#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdarg>

class UsbCdcTransport {
public:
    static constexpr uint8_t kTransferFrameMarker = 0x7E;
    static constexpr size_t kTransferFrameHeaderSize = 9;
    static constexpr size_t kMaxTransferPayloadSize = 256;

    enum class PacketKind
    {
        None,
        Line,
        Frame
    };

    struct FramePacket
    {
        uint8_t type = 0;
        uint8_t transfer_id = 0;
        uint8_t flags = 0;
        uint32_t seq = 0;
        uint16_t payload_len = 0;
        uint8_t payload[kMaxTransferPayloadSize]{};
    };

    PacketKind poll(char* line_buf, size_t line_max, FramePacket& frame);

    void send_line(const char* text);
    void send_fmt(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void send_frame(uint8_t type, uint8_t transfer_id, uint32_t seq,
                    const uint8_t* payload, uint16_t payload_len);

private:
    enum class ReceiveMode
    {
        Idle,
        Line,
        FrameHeader,
        FrameBody
    };

    ReceiveMode mode_ = ReceiveMode::Idle;
    char line_[2048]{};
    size_t line_len_ = 0;
    uint8_t frame_header_[kTransferFrameHeaderSize]{};
    size_t frame_header_len_ = 0;
    uint8_t frame_body_[kMaxTransferPayloadSize + sizeof(uint32_t)]{};
    size_t frame_body_len_ = 0;
    size_t frame_expected_body_len_ = 0;
    uint16_t frame_payload_len_ = 0;

    static void write_u16_le(uint8_t* out, uint16_t value);
    static void write_u32_le(uint8_t* out, uint32_t value);
    static uint16_t read_u16_le(const uint8_t* in);
    static uint32_t read_u32_le(const uint8_t* in);
    static uint32_t crc32(const uint8_t* data, size_t len);
    void reset_frame_parse();
    void reset_receive_state();
};
