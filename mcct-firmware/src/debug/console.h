/*
 * Debug console on UART1 (TX = P1.6 / pin 8, RX = P0.2 / pin 18). These are
 * the ICP pins, so with the Arduino bridge attached the console appears on
 * the bridge's USB port between flashes (mcct.py monitor).
 * Output blocks (38400 baud: ~0.26 ms per character).
 */
#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include "common/config.h"

#if CFG_CONSOLE
void console_init(void);
int16_t console_getc(void);             /* -1 if nothing received */
void console_putc(char c);              /* '\n' becomes "\r\n" */
void console_puts(const char *s);
void console_put_int(int32_t v);
#else
#define console_init()          ((void)0)
#define console_getc()          (-1)
#define console_putc(c)         ((void)0)
#define console_puts(s)         ((void)0)
#define console_put_int(v)      ((void)0)
#endif

#endif
