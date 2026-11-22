/* Stand-ins for the hardware drivers the pure modules call. */
#include "test.h"
#include "board/board.h"
#include "board/tick.h"
#include "display/display.h"
#include "display/render.h"
#include "storage/flash.h"

__xdata uint16_t fake_now;
__xdata uint8_t fake_heater_pin;
__xdata uint8_t fake_frame[3];

/* Two settings pages of fake flash. */
static __xdata uint8_t flash[2 * FLASH_PAGE_SIZE];

ms_t clock_ms(void)
{
    return fake_now;
}

void board_heater_write(bool on)
{
    fake_heater_pin = on;
}

void display_write(const __xdata uint8_t *frame)
{
    fake_frame[0] = frame[0];
    fake_frame[1] = frame[1];
    fake_frame[2] = frame[2];
}

uint8_t flash_read(uint16_t addr)
{
    return flash[addr - SETTINGS_FLASH_BASE];
}

void flash_erase_page(uint16_t addr)
{
    uint16_t base = (addr - SETTINGS_FLASH_BASE) & ~(FLASH_PAGE_SIZE - 1);
    uint8_t i;
    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        flash[base + i] = 0xFF;
}

void flash_write(uint16_t addr, uint8_t value)
{
    flash[addr - SETTINGS_FLASH_BASE] &= value;     /* flash can only clear bits */
}

void fake_flash_erase_all(void)
{
    uint16_t i;
    for (i = 0; i < sizeof(flash); i++)
        flash[i] = 0xFF;
}

void fake_flash_poke(uint16_t addr, uint8_t value)
{
    flash[addr - SETTINGS_FLASH_BASE] = value;
}

static bool frame_matches_render(void)
{
    return fake_frame[0] == render_frame[0] && fake_frame[1] == render_frame[1]
        && fake_frame[2] == render_frame[2];
}

bool frame_is_text(const char *text)
{
    render_text(text);
    return frame_matches_render();
}

/* Ignores decimal points (the heater indicator). */
bool frame_is_int(int16_t v)
{
    render_int(v);
    return (fake_frame[0] & 0x7F) == render_frame[0] && (fake_frame[1] & 0x7F) == render_frame[1]
        && (fake_frame[2] & 0x7F) == render_frame[2];
}

bool frame_is_blank(void)
{
    return !fake_frame[0] && !fake_frame[1] && !fake_frame[2];
}
