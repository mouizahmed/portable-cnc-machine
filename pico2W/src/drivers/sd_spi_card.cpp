#include "drivers/sd_spi_card.h"

#include <array>
#include <cstring>

#include "config.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

namespace {
constexpr uint8_t kCmd0 = 0;
constexpr uint8_t kCmd1 = 1;
constexpr uint8_t kCmd8 = 8;
constexpr uint8_t kCmd9 = 9;
constexpr uint8_t kCmd16 = 16;
constexpr uint8_t kCmd17 = 17;
constexpr uint8_t kCmd24 = 24;
constexpr uint8_t kCmd25 = 25;
constexpr uint8_t kCmd55 = 55;
constexpr uint8_t kCmd58 = 58;
constexpr uint8_t kAcmd23 = 23;
constexpr uint8_t kAcmd41 = 41;

constexpr uint8_t kR1Idle = 0x01;
constexpr uint8_t kDataToken = 0xFE;
constexpr uint8_t kMultiBlockWriteToken = 0xFC;
constexpr uint8_t kStopTranToken = 0xFD;
constexpr uint8_t kDataAccepted = 0x05;
constexpr uint32_t kInitTimeoutMs = 1000;
constexpr uint32_t kReadTimeoutMs = 200;
constexpr uint32_t kWriteTimeoutMs = 500;

SdSpiCard* g_active_card = nullptr;

void set_pin(unsigned int pin, bool level) {
    gpio_put(pin, level ? 1 : 0);
}
}  // namespace

SdSpiCard::SdSpiCard(spi_inst_t* spi) : spi_(spi) {}

void SdSpiCard::init() {
    spi_init(spi_, SD_SPI_INIT_BAUD);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_SD_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_SPI_MISO, GPIO_FUNC_SPI);

    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    set_pin(PIN_SD_CS, true);
    sd_spi_card_set_active(this);
}

bool SdSpiCard::initialize_card() {
    initialized_ = false;
    high_capacity_ = false;
    sector_count_ = 0;

    begin_bus(SD_SPI_INIT_BAUD);
    set_pin(PIN_SD_CS, true);
    clock_idle_bytes(10);
    end_bus();

    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint8_t response[4]{};
    do {
        if (send_command(kCmd0, 0) == kR1Idle) {
            break;
        }
    } while ((to_ms_since_boot(get_absolute_time()) - start) < kInitTimeoutMs);

    if (send_command(kCmd0, 0) != kR1Idle) {
        return false;
    }

    const uint8_t cmd8_response = send_command(kCmd8, 0x1AA, response, sizeof(response));
    if (cmd8_response == kR1Idle && response[2] == 0x01 && response[3] == 0xAA) {
        start = to_ms_since_boot(get_absolute_time());
        do {
            if (send_command(kAcmd41, 0x40000000) == 0x00) {
                break;
            }
        } while ((to_ms_since_boot(get_absolute_time()) - start) < kInitTimeoutMs);

        if (send_command(kCmd58, 0, response, sizeof(response)) != 0x00) {
            return false;
        }
        high_capacity_ = (response[0] & 0x40) != 0;
    } else {
        start = to_ms_since_boot(get_absolute_time());
        uint8_t init_response = 0xFF;
        do {
            init_response = send_command(kAcmd41, 0);
            if (init_response == 0x00) {
                break;
            }
            if (init_response > 0x01) {
                init_response = send_command(kCmd1, 0);
                if (init_response == 0x00) {
                    break;
                }
            }
        } while ((to_ms_since_boot(get_absolute_time()) - start) < kInitTimeoutMs);

        if (init_response != 0x00) {
            return false;
        }

        if (send_command(kCmd16, 512) != 0x00) {
            return false;
        }
    }

    if (!read_csd()) {
        return false;
    }

    initialized_ = true;
    begin_bus(SD_SPI_BAUD);
    end_bus();
    return true;
}

bool SdSpiCard::is_initialized() const {
    return initialized_;
}

bool SdSpiCard::read_blocks(uint32_t sector, uint8_t* buffer, std::size_t count) {
    if (!initialized_ || buffer == nullptr || count == 0) {
        return false;
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (send_command(kCmd17, block_address(sector + static_cast<uint32_t>(index)), nullptr, 0, true) != 0x00) {
            return false;
        }
        if (!read_data_block(buffer + (index * 512), 512)) {
            end_bus();
            return false;
        }
        end_bus();
    }

    return true;
}

bool SdSpiCard::write_blocks(uint32_t sector, const uint8_t* buffer, std::size_t count) {
    if (!initialized_ || buffer == nullptr || count == 0) {
        return false;
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (send_command(kCmd24, block_address(sector + static_cast<uint32_t>(index)), nullptr, 0, true) != 0x00) {
            return false;
        }
        if (!write_data_block(buffer + (index * 512), kDataToken)) {
            end_bus();
            return false;
        }
        end_bus();
    }

    return true;
}

bool SdSpiCard::sync() {
    if (!initialized_) {
        return false;
    }

    begin_bus(SD_SPI_BAUD);
    const bool ready = wait_ready(kWriteTimeoutMs);
    end_bus();
    return ready;
}

uint32_t SdSpiCard::sector_count() const {
    return sector_count_;
}

void SdSpiCard::begin_bus(uint32_t baudrate) {
    spi_set_baudrate(spi_, baudrate);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    set_pin(PIN_SD_CS, false);
}

void SdSpiCard::end_bus() {
    set_pin(PIN_SD_CS, true);
    transfer(0xFF);
}

uint8_t SdSpiCard::transfer(uint8_t value) {
    uint8_t rx = 0xFF;
    spi_write_read_blocking(spi_, &value, &rx, 1);
    return rx;
}

void SdSpiCard::clock_idle_bytes(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        transfer(0xFF);
    }
}

bool SdSpiCard::wait_ready(uint32_t timeout_ms) {
    const uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        if (transfer(0xFF) == 0xFF) {
            return true;
        }
    }
    return false;
}

bool SdSpiCard::read_data_block(uint8_t* buffer, std::size_t length) {
    const uint32_t start = to_ms_since_boot(get_absolute_time());
    uint8_t token = 0xFF;
    while ((to_ms_since_boot(get_absolute_time()) - start) < kReadTimeoutMs) {
        token = transfer(0xFF);
        if (token == kDataToken) {
            break;
        }
    }

    if (token != kDataToken) {
        return false;
    }

    spi_read_blocking(spi_, 0xFF, buffer, static_cast<size_t>(length));

    transfer(0xFF);
    transfer(0xFF);
    return true;
}

bool SdSpiCard::write_data_block(const uint8_t* buffer, uint8_t token) {
    if (!wait_ready(kWriteTimeoutMs)) {
        return false;
    }

    transfer(token);
    spi_write_blocking(spi_, buffer, 512);
    transfer(0xFF);
    transfer(0xFF);

    const uint8_t response = static_cast<uint8_t>(transfer(0xFF) & 0x1F);
    return response == kDataAccepted && wait_ready(kWriteTimeoutMs);
}

uint8_t SdSpiCard::send_command(uint8_t command,
                                uint32_t argument,
                                uint8_t* response,
                                std::size_t response_size,
                                bool keep_selected) {
    if (command == kAcmd23 || command == kAcmd41) {
        const uint8_t prefix = send_command(kCmd55, 0);
        if (prefix > 0x01) {
            return prefix;
        }
    }

    begin_bus(initialized_ ? SD_SPI_BAUD : SD_SPI_INIT_BAUD);
    if (!wait_ready(kInitTimeoutMs)) {
        end_bus();
        return 0xFF;
    }

    std::array<uint8_t, 6> frame{
        static_cast<uint8_t>(0x40U | command),
        static_cast<uint8_t>(argument >> 24),
        static_cast<uint8_t>(argument >> 16),
        static_cast<uint8_t>(argument >> 8),
        static_cast<uint8_t>(argument),
        0x01,
    };

    if (command == kCmd0) {
        frame[5] = 0x95;
    } else if (command == kCmd8) {
        frame[5] = 0x87;
    }

    for (uint8_t byte : frame) {
        transfer(byte);
    }

    uint8_t result = 0xFF;
    for (int attempt = 0; attempt < 10; ++attempt) {
        result = transfer(0xFF);
        if ((result & 0x80) == 0) {
            break;
        }
    }

    if (response != nullptr) {
        for (std::size_t i = 0; i < response_size; ++i) {
            response[i] = transfer(0xFF);
        }
    }

    if (!keep_selected) {
        end_bus();
    }
    return result;
}

uint32_t SdSpiCard::block_address(uint32_t sector) const {
    return high_capacity_ ? sector : (sector * 512u);
}

bool SdSpiCard::read_csd() {
    if (send_command(kCmd9, 0, nullptr, 0, true) != 0x00) {
        return false;
    }

    std::array<uint8_t, 16> csd{};
    if (!read_data_block(csd.data(), csd.size())) {
        end_bus();
        return false;
    }
    end_bus();

    const uint8_t csd_version = static_cast<uint8_t>((csd[0] >> 6) & 0x03);
    if (csd_version == 1) {
        const uint32_t c_size =
            (static_cast<uint32_t>(csd[7] & 0x3F) << 16) |
            (static_cast<uint32_t>(csd[8]) << 8) |
            static_cast<uint32_t>(csd[9]);
        sector_count_ = (c_size + 1u) * 1024u;
        return true;
    }

    const uint32_t read_bl_len = static_cast<uint32_t>(csd[5] & 0x0F);
    const uint32_t c_size =
        (static_cast<uint32_t>(csd[6] & 0x03) << 10) |
        (static_cast<uint32_t>(csd[7]) << 2) |
        (static_cast<uint32_t>(csd[8] & 0xC0) >> 6);
    const uint32_t c_size_mult =
        (static_cast<uint32_t>(csd[9] & 0x03) << 1) |
        (static_cast<uint32_t>(csd[10] & 0x80) >> 7);
    const uint32_t block_len = 1u << read_bl_len;
    const uint32_t block_count = (c_size + 1u) << (c_size_mult + 2u);
    const uint64_t capacity_bytes = static_cast<uint64_t>(block_count) * block_len;
    sector_count_ = static_cast<uint32_t>(capacity_bytes / 512u);
    return sector_count_ > 0;
}

void sd_spi_card_set_active(SdSpiCard* card) {
    g_active_card = card;
}

SdSpiCard* sd_spi_card_active() {
    return g_active_card;
}
