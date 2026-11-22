/*
 * 2 kHz system tick (Timer2). The interrupt multiplexes the display, samples
 * the buttons and the ADC, and counts milliseconds. Nothing else runs in
 * interrupt context: all logic runs in the main loop.
 */
#ifndef BOARD_TICK_H
#define BOARD_TICK_H

#include "common/types.h"

#define TICK_HZ     2000u

void tick_init(void);           /* starts the tick and enables interrupts */
ms_t clock_ms(void);            /* milliseconds since boot (wraps at 65536) */

/* SDCC needs every ISR prototype visible in the file that defines main(). */
void tick_isr(void) __interrupt(5);     /* Timer2 overflow vector */

#endif
