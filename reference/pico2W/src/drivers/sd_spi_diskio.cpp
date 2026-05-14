#include "drivers/sd_spi_card.h"

extern "C" {
#include "ff.h"
#include "diskio.h"
}

namespace {
constexpr BYTE kSdDriveNumber = 0;
}

extern "C" DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != kSdDriveNumber) {
        return STA_NOINIT;
    }

    SdSpiCard* card = sd_spi_card_active();
    if (card == nullptr || !card->is_initialized()) {
        return STA_NOINIT;
    }

    return 0;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != kSdDriveNumber) {
        return STA_NOINIT;
    }

    SdSpiCard* card = sd_spi_card_active();
    if (card == nullptr) {
        return STA_NOINIT;
    }

    return card->initialize_card() ? 0 : STA_NOINIT;
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != kSdDriveNumber || buff == nullptr || count == 0) {
        return RES_PARERR;
    }

    SdSpiCard* card = sd_spi_card_active();
    if (card == nullptr || !card->is_initialized()) {
        return RES_NOTRDY;
    }

    return card->read_blocks(static_cast<uint32_t>(sector), buff, count) ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
extern "C" DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != kSdDriveNumber || buff == nullptr || count == 0) {
        return RES_PARERR;
    }

    SdSpiCard* card = sd_spi_card_active();
    if (card == nullptr || !card->is_initialized()) {
        return RES_NOTRDY;
    }

    return card->write_blocks(static_cast<uint32_t>(sector), buff, count) ? RES_OK : RES_ERROR;
}
#endif

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != kSdDriveNumber) {
        return RES_PARERR;
    }

    SdSpiCard* card = sd_spi_card_active();
    if (card == nullptr || !card->is_initialized()) {
        return RES_NOTRDY;
    }

    switch (cmd) {
        case CTRL_SYNC:
            return card->sync() ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            if (buff == nullptr) {
                return RES_PARERR;
            }
            *static_cast<LBA_t*>(buff) = static_cast<LBA_t>(card->sector_count());
            return RES_OK;
        case GET_SECTOR_SIZE:
            if (buff == nullptr) {
                return RES_PARERR;
            }
            *static_cast<WORD*>(buff) = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if (buff == nullptr) {
                return RES_PARERR;
            }
            *static_cast<DWORD*>(buff) = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
