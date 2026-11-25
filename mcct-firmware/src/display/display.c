#include "display/display.h"
#include "display/font.h"
#include "board/pins.h"

#define SEGMENT_SLOTS   8
#define BUTTON_SLOT     SEGMENT_SLOTS       /* everything dark: lines settle */

/* columns[s] bit d set = digit d shows segment s. Read by the ISR. */
static __idata uint8_t columns[SEGMENT_SLOTS];  /* indexed: @R0 access anyway */
static __idata uint8_t slot;
static volatile __data uint8_t buttons_raw;

void display_init(void)
{
    uint8_t s;
    for (s = 0; s < SEGMENT_SLOTS; s++)
        columns[s] = 0;
}

void display_write(const __xdata uint8_t *frame)
{
    uint8_t s, mask, col;

    /* Transpose digits x segments into segments x digits. Each byte is
     * written in one instruction, so the ISR never sees a torn column;
     * a frame mixing old and new columns lasts at most 4.5 ms. */
    for (s = 0, mask = 1; s < SEGMENT_SLOTS; s++, mask <<= 1) {
        col = 0;
        if (frame[0] & mask) col |= 0x01;
        if (frame[1] & mask) col |= 0x02;
        if (frame[2] & mask) col |= 0x04;
        columns[s] = col;
    }
}

uint8_t display_buttons_raw(void)
{
    return buttons_raw;
}

/* Runs in the tick interrupt: no locals, no calls. */
void display_scan(void)
{
    /* All dark: anodes low, every cathode released (quasi high). */
    PIN_DIGIT1 = 0;
    PIN_DIGIT2 = 0;
    PIN_DIGIT3 = 0;
    PIN_SEG_A = 1; PIN_SEG_B = 1; PIN_SEG_C = 1; PIN_SEG_D = 1;
    PIN_SEG_E = 1; PIN_SEG_F = 1; PIN_SEG_G = 1; PIN_SEG_DP = 1;

    if (slot == BUTTON_SLOT) {
        slot = 0;               /* stay dark for one slot so the lines settle */
        return;
    }
    if (slot == 0) {
        /* The previous slot was dark: only a pressed button can hold a
         * quasi-mode line low now. */
        buttons_raw = (PIN_BTN_ENTER ? 0 : BUTTON_RAW_ENTER)
                    | (PIN_BTN_UP    ? 0 : BUTTON_RAW_UP)
                    | (PIN_BTN_DOWN  ? 0 : BUTTON_RAW_DOWN)
                    | (PIN_BTN_SET   ? 0 : BUTTON_RAW_SET);
    }

    switch (slot) {
    case 0: PIN_SEG_A  = 0; break;
    case 1: PIN_SEG_B  = 0; break;
    case 2: PIN_SEG_C  = 0; break;
    case 3: PIN_SEG_D  = 0; break;
    case 4: PIN_SEG_E  = 0; break;
    case 5: PIN_SEG_F  = 0; break;
    case 6: PIN_SEG_G  = 0; break;
    default: PIN_SEG_DP = 0; break;
    }
    if (columns[slot] & 0x01) PIN_DIGIT1 = 1;
    if (columns[slot] & 0x02) PIN_DIGIT2 = 1;
    if (columns[slot] & 0x04) PIN_DIGIT3 = 1;
    slot++;
}
