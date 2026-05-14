#include "protocol/app_cdc_transport.h"

#include <Arduino.h>
#include <string.h>

static uint32_t crc32_update_table(uint32_t crc, const uint8_t *data, size_t len)
{
    static bool initialized = false;
    static uint32_t table[256];

    if(!initialized) {
        for(uint32_t value = 0; value < 256; value++) {
            uint32_t entry = value;
            for(int bit = 0; bit < 8; bit++)
                entry = (entry & 1u) != 0 ? (entry >> 1) ^ 0xEDB88320u : (entry >> 1);
            table[value] = entry;
        }
        initialized = true;
    }

    for(size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

    return crc;
}

bool AppCdcTransport::poll(Frame& frame)
{
#if defined(CDC2_STATUS_INTERFACE) && defined(CDC2_DATA_INTERFACE)
    while(SerialUSB1.available() > 0) {
        const int value = SerialUSB1.read();
        if(value < 0)
            return false;

        const uint8_t byte = (uint8_t)value;

        switch(mode_) {
            case ReceiveMode::Idle:
                if(byte == kFrameMarker) {
                    reset_frame_parse();
                    mode_ = ReceiveMode::Frame;
                }
                break;

            case ReceiveMode::Frame:
                if(byte == kFrameMarker) {
                    if(frame_packet_len_ == 0) {
                        reset_frame_parse();
                        break;
                    }

                    if(decode_frame(frame))
                        return true;

                    reset_receive_state();
                    break;
                }

                if(byte == kFrameEscape) {
                    mode_ = ReceiveMode::Escape;
                    break;
                }

                append_frame_byte(byte);
                break;

            case ReceiveMode::Escape:
                append_frame_byte((uint8_t)(byte ^ kFrameEscapeXor));
                if(mode_ == ReceiveMode::Escape)
                    mode_ = ReceiveMode::Frame;
                break;
        }
    }
#endif

    return false;
}

bool AppCdcTransport::connected() const
{
#if defined(CDC2_STATUS_INTERFACE) && defined(CDC2_DATA_INTERFACE)
    return SerialUSB1.dtr() != 0;
#else
    return false;
#endif
}

void AppCdcTransport::send_frame(uint8_t type, uint8_t transfer_id, uint32_t seq,
                                 const uint8_t *payload, uint16_t payload_len)
{
#if defined(CDC2_STATUS_INTERFACE) && defined(CDC2_DATA_INTERFACE)
    if(payload_len > kMaxPayloadSize)
        return;

    size_t offset = 0;
    tx_raw_[offset++] = type;
    tx_raw_[offset++] = transfer_id;
    tx_raw_[offset++] = 0;
    write_u32_le(tx_raw_ + offset, seq);
    offset += sizeof(uint32_t);
    write_u16_le(tx_raw_ + offset, payload_len);
    offset += sizeof(uint16_t);

    if(payload_len > 0 && payload != nullptr) {
        memcpy(tx_raw_ + offset, payload, payload_len);
        offset += payload_len;
    }

    const uint32_t crc = crc32(tx_raw_, kFrameHeaderSize + payload_len);
    write_u32_le(tx_raw_ + offset, crc);
    offset += sizeof(uint32_t);

    const size_t encoded_len = cobs_encode(tx_raw_, offset, tx_encoded_, sizeof(tx_encoded_));
    if(encoded_len == 0)
        return;

    size_t wire_len = 0;
    tx_wire_[wire_len++] = kFrameMarker;
    for(size_t i = 0; i < encoded_len; i++) {
        if(tx_encoded_[i] == kFrameMarker ||
           tx_encoded_[i] == kFrameEscape ||
           tx_encoded_[i] == '\r' ||
           tx_encoded_[i] == '\n') {
            tx_wire_[wire_len++] = kFrameEscape;
            tx_wire_[wire_len++] = (uint8_t)(tx_encoded_[i] ^ kFrameEscapeXor);
            continue;
        }
        tx_wire_[wire_len++] = tx_encoded_[i];
    }
    tx_wire_[wire_len++] = kFrameMarker;

    SerialUSB1.write(tx_wire_, wire_len);
    SerialUSB1.flush();
#else
    (void)type;
    (void)transfer_id;
    (void)seq;
    (void)payload;
    (void)payload_len;
#endif
}

void AppCdcTransport::write_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

void AppCdcTransport::write_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

uint16_t AppCdcTransport::read_u16_le(const uint8_t *in)
{
    return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}

uint32_t AppCdcTransport::read_u32_le(const uint8_t *in)
{
    return (uint32_t)in[0]
         | ((uint32_t)in[1] << 8)
         | ((uint32_t)in[2] << 16)
         | ((uint32_t)in[3] << 24);
}

uint32_t AppCdcTransport::crc32(const uint8_t *data, size_t len)
{
    return ~crc32_update_table(0xFFFFFFFFu, data, len);
}

size_t AppCdcTransport::cobs_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap)
{
    if(input == nullptr || output == nullptr || output_cap == 0)
        return 0;

    size_t read = 0;
    size_t write = 1;
    size_t code_index = 0;
    uint8_t code = 1;

    while(read < input_len) {
        if(write >= output_cap)
            return 0;

        if(input[read] == 0) {
            output[code_index] = code;
            code = 1;
            code_index = write++;
            read++;
            continue;
        }

        output[write++] = input[read++];
        code++;

        if(code == 0xFF) {
            output[code_index] = code;
            code = 1;
            code_index = write++;
        }
    }

    if(code_index >= output_cap)
        return 0;

    output[code_index] = code;
    return write;
}

int AppCdcTransport::cobs_decode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap)
{
    if(input == nullptr || output == nullptr)
        return -1;

    size_t read = 0;
    size_t write = 0;

    while(read < input_len) {
        const uint8_t code = input[read++];
        if(code == 0)
            return -1;

        const size_t copy = (size_t)(code - 1);
        if(read + copy > input_len || write + copy > output_cap)
            return -1;

        if(copy > 0)
            memcpy(output + write, input + read, copy);

        read += copy;
        write += copy;

        if(code < 0xFF && read < input_len) {
            if(write >= output_cap)
                return -1;
            output[write++] = 0;
        }
    }

    return (int)write;
}

bool AppCdcTransport::append_frame_byte(uint8_t byte)
{
    if(frame_packet_len_ >= sizeof(frame_packet_)) {
        reset_receive_state();
        return false;
    }

    frame_packet_[frame_packet_len_++] = byte;
    return true;
}

bool AppCdcTransport::decode_frame(Frame& frame)
{
    const int decoded_len = cobs_decode(frame_packet_, frame_packet_len_,
                                        decoded_frame_, sizeof(decoded_frame_));

    if(decoded_len < (int)(kFrameHeaderSize + sizeof(uint32_t)))
        return false;

    const uint16_t payload_len = read_u16_le(decoded_frame_ + 7);
    const size_t expected_len = kFrameHeaderSize + payload_len + sizeof(uint32_t);

    if(payload_len > kMaxPayloadSize || (size_t)decoded_len != expected_len)
        return false;

    const uint32_t expected_crc = read_u32_le(decoded_frame_ + kFrameHeaderSize + payload_len);
    if(crc32(decoded_frame_, kFrameHeaderSize + payload_len) != expected_crc)
        return false;

    frame.type = decoded_frame_[0];
    frame.transfer_id = decoded_frame_[1];
    frame.flags = decoded_frame_[2];
    frame.seq = read_u32_le(decoded_frame_ + 3);
    frame.payload_len = payload_len;

    if(payload_len > 0)
        memcpy(frame.payload, decoded_frame_ + kFrameHeaderSize, payload_len);

    reset_receive_state();
    return true;
}

void AppCdcTransport::reset_frame_parse()
{
    frame_packet_len_ = 0;
}

void AppCdcTransport::reset_receive_state()
{
    mode_ = ReceiveMode::Idle;
    reset_frame_parse();
}
