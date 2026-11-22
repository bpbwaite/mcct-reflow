#include "storage/flash.h"
#include "board/pins.h"

uint8_t flash_read(uint16_t addr)
{
    return *(const __code uint8_t *)addr;
}

static void iap(uint8_t command, uint16_t addr, uint8_t data)
{
    TA_SET_BITS(CHPCON, CHPCON_IAPEN);
    TA_SET_BITS(IAPUEN, IAPUEN_APUEN);
    IAPCN = command;
    IAPAH = (uint8_t)(addr >> 8);
    IAPAL = (uint8_t)addr;
    IAPFD = data;
    TA_SET_BITS(IAPTRG, IAPTRG_IAPGO);      /* CPU stalls until done */
    TA_CLR_BITS(IAPUEN, IAPUEN_APUEN);
    TA_CLR_BITS(CHPCON, CHPCON_IAPEN);
}

void flash_erase_page(uint16_t addr)
{
    iap(IAP_ERASE_APROM_PAGE, addr, 0xFF);
}

void flash_write(uint16_t addr, uint8_t value)
{
    iap(IAP_PROGRAM_APROM_BYTE, addr, value);
}
