/*
 * Debug console on UART1 (TX = P1.6 / pin 8, RX = P0.2 / pin 18).
 *
 * These are the ICP data/clock pins, so with the Arduino bridge connected the
 * console is available on the bridge's USB port between flashes
 * (.\build.ps1 -Monitor). The bridge's passthrough runs at CONSOLE_BAUD.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

#ifndef CONSOLE_BAUD
#define CONSOLE_BAUD 38400UL    /* must match PASSTHROUGH_BAUD in the bridge sketch */
#endif

/* Configure UART1 pins and baud. fsys_hz is the current system clock. */
void console_init(uint32_t fsys_hz);

/* Non-blocking read: returns the received byte, or -1 if none is waiting. */
int16_t console_getc(void);

/* putchar()/getchar() are routed to UART1, so printf_tiny()/printf() print to
 * the console. '\n' is sent as "\r\n". getchar() blocks. */

#endif
