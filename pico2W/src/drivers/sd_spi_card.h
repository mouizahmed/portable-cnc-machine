#pragma once

#include <cstddef>
#include <cstdint>

#include "hardware/spi.h"

class SdSpiCard {
public:
    explicit SdSpiCard(spi_inst_t* spi);

    void init();
    bool initialize_card();
    bool is_initialized() const;
    bool read_blocks(uint32_t sector, uint8_t* buffer, std::size_t count);
    bool write_blocks(uint32_t sector, const uint8_t* buffer, std::size_t count);
    bool sync();
    uint32_t sector_count() const;

private:
    spi_inst_t* spi_;
    bool initialized_ = false;
    bool high_capacity_ = false;
    uint32_t sector_count_ = 0;

    void begin_bus(uint32_t baudrate);
    void end_bus();
    uint8_t transfer(uint8_t value);
    void clock_idle_bytes(std::size_t count);
    bool wait_ready(uint32_t timeout_ms);
    bool read_data_block(uint8_t* buffer, std::size_t length);
    bool write_data_block(const uint8_t* buffer, uint8_t token);
    uint8_t send_command(uint8_t command,
                         uint32_t argument,
                         uint8_t* response = nullptr,
                         std::size_t response_size = 0,
                         bool keep_selected = false);
    uint32_t block_address(uint32_t sector) const;
    bool read_csd();
};

void sd_spi_card_set_active(SdSpiCard* card);
SdSpiCard* sd_spi_card_active();
