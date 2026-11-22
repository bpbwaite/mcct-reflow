#include "debug/console.h"

#if CFG_CONSOLE
#include "board/board.h"
#include "board/pins.h"

#define UART1_MODE1_RX      0x50            /* mode 1, REN */
#define T3CON_SMOD1_RUN     0x88            /* SMOD_1 (double rate) | TR3, prescale 1 */
#define T3_RELOAD           (65536u - (uint16_t)((FSYS_HZ / 16 + CFG_CONSOLE_BAUD / 2) / CFG_CONSOLE_BAUD))

void console_init(void)
{
    SCON_1 = UART1_MODE1_RX;
    RH3 = (uint8_t)(T3_RELOAD >> 8);
    RL3 = (uint8_t)T3_RELOAD;
    T3CON = T3CON_SMOD1_RUN;
    TI_1 = 1;                               /* transmitter idle */
}

int16_t console_getc(void)
{
    uint8_t c;
    if (!RI_1)
        return -1;
    c = SBUF_1;
    RI_1 = 0;
    return c;
}

static void tx(char c)
{
    while (!TI_1)
        ;
    TI_1 = 0;
    SBUF_1 = (uint8_t)c;
}

void console_putc(char c)
{
    if (c == '\n')
        tx('\r');
    tx(c);
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void console_put_int(int32_t v)
{
    static __xdata char buf[10];
    uint8_t n = 0;
    uint32_t u;

    if (v < 0) {
        console_putc('-');
        u = (uint32_t)(-v);
    } else {
        u = (uint32_t)v;
    }
    do {
        uint32_t q = u / 10;                /* no separate modulo: saves a library routine */
        buf[n++] = (char)('0' + (uint8_t)(u - q * 10));
        u = q;
    } while (u);
    while (n)
        console_putc(buf[--n]);
}
#endif
