#include <stdio.h>
#include "numicro_8051.h"
#include "console.h"

void console_init(uint32_t fsys_hz)
{
    P16_QUASI_MODE;                     /* UART1_TXD (ICPDA) */
    P02_QUASI_MODE;                     /* UART1_RXD (ICPCK) */
    UART_Open(fsys_hz, UART1_Timer3, CONSOLE_BAUD);   /* mode 1, RX enabled */
}

static void tx_byte(uint8_t c)
{
    SBUF_1 = c;
    while (!TI_1)
        ;
    TI_1 = 0;
}

/* Overrides the BSP's UART0 putchar/getchar (sdcc_stdio.c in ms51bsp.lib). */
int putchar(int c)
{
    if (c == '\n')
        tx_byte('\r');
    tx_byte((uint8_t)c);
    return c;
}

int getchar(void)
{
    uint8_t c;
    while (!RI_1)
        ;
    c = SBUF_1;
    RI_1 = 0;
    return c;
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
