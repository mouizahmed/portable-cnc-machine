#pragma once

#include <stddef.h>
#include <stdint.h>

class AppCdcTransport {
public:
    static constexpr uint8_t kFrameMarker = 0x7E;
    static constexpr uint8_t kFrameEscape = 0x7D;
    static constexpr uint8_t kFrameEscapeXor = 0x20;
    static constexpr size_t kFrameHeaderSize = 9;
    static constexpr size_t kMaxPayloadSize = 4096;
    static constexpr size_t kMaxRawFrameSize = kFrameHeaderSize + kMaxPayloadSize + sizeof(uint32_t);
    static constexpr size_t kMaxEncodedFrameSize = kMaxRawFrameSize + ((kMaxRawFrameSize + 253) / 254);

    struct Frame {
        uint8_t type = 0;
        uint8_t transfer_id = 0;
        uint8_t flags = 0;
        uint32_t seq = 0;
        uint16_t payload_len = 0;
        uint8_t payload[kMaxPayloadSize]{};
    };

    bool poll(Frame& frame);
    bool connected() const;
    void send_frame(uint8_t type, uint8_t transfer_id, uint32_t seq,
                    const uint8_t *payload, uint16_t payload_len);

private:
    enum class ReceiveMode {
        Idle,
        Frame,
        Escape
    };

    ReceiveMode mode_ = ReceiveMode::Idle;
    uint8_t frame_packet_[kMaxEncodedFrameSize]{};
    size_t frame_packet_len_ = 0;
    uint8_t decoded_frame_[kMaxRawFrameSize]{};
    uint8_t tx_raw_[kMaxRawFrameSize]{};
    uint8_t tx_encoded_[kMaxEncodedFrameSize]{};
    uint8_t tx_wire_[1 + (kMaxEncodedFrameSize * 2) + 1]{};

    static void write_u16_le(uint8_t *out, uint16_t value);
    static void write_u32_le(uint8_t *out, uint32_t value);
    static uint16_t read_u16_le(const uint8_t *in);
    static uint32_t read_u32_le(const uint8_t *in);
    static uint32_t crc32(const uint8_t *data, size_t len);
    static size_t cobs_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap);
    static int cobs_decode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap);

    bool append_frame_byte(uint8_t byte);
    bool decode_frame(Frame& frame);
    void reset_frame_parse();
    void reset_receive_state();
};
