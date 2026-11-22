#include "board/tick.h"
#include "board/board.h"
#include "board/pins.h"
#include "display/display.h"
#include "sensor/adc.h"

#define T2_DIV              4u
#define T2_RELOAD           (65536u - (uint16_t)(FSYS_HZ / T2_DIV / TICK_HZ))
#define T2MOD_AUTO_RELOAD   0x88    /* LDEN | reload on overflow */
#define T2MOD_DIV_4         0x10
#define EIE_ET2             0x80

static volatile uint16_t milliseconds;
static uint8_t half_ms;

void tick_init(void)
{
    T2CON &= (uint8_t)~0x01;                    /* CM/RL2 = 0: auto-reload */
    T2MOD = T2MOD_AUTO_RELOAD | T2MOD_DIV_4;
    RCMP2H = (uint8_t)(T2_RELOAD >> 8);
    RCMP2L = (uint8_t)T2_RELOAD;
    TH2 = (uint8_t)(T2_RELOAD >> 8);
    TL2 = (uint8_t)T2_RELOAD;
    TF2 = 0;
    TR2 = 1;
    EIE |= EIE_ET2;
    EA = 1;                                     /* global interrupt enable */
}

ms_t clock_ms(void)
{
    ms_t now;
    __bit ea = EA;
    EA = 0;
    now = milliseconds;
    EA = ea;
    return now;
}

void tick_isr(void) __interrupt(5)
{
    TF2 = 0;
    display_scan();             /* switch LEDs first ...                   */
    adc_scan();                 /* ... so the conversion runs on a quiet rail */
    if (++half_ms & 1)
        return;
    milliseconds++;
}
