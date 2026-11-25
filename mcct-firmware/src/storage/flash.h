/*
 * APROM self-programming (IAP). The settings live in the last two 128-byte
 * pages of the 16 KB APROM; mcct.py keeps code out of them by linking with
 * --code-size SETTINGS_FLASH_BASE and passes the address in as a define.
 * The CPU halts while the flash is busy (a few ms per erase).
 */
#ifndef STORAGE_FLASH_H
#define STORAGE_FLASH_H

#include <stdint.h>

#define FLASH_PAGE_SIZE     128u

#ifndef SETTINGS_FLASH_BASE
#error "SETTINGS_FLASH_BASE must come from mcct.py"
#endif

uint8_t flash_read(uint16_t addr);
void flash_erase_page(uint16_t addr);
void flash_write(uint16_t addr, uint8_t value);

#endif
