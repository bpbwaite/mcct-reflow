/*
 * 5361BS 3-digit common-anode display, multiplexed by segment.
 *
 * The current-limiting resistors sit on the digit anodes and the segment
 * cathodes connect straight to the MCU. Scanning one SEGMENT at a time (with
 * up to three anodes on) gives every lit LED its own resistor, so brightness
 * does not depend on how many segments a digit shows ("1" vs "8"), which
 * digit-at-a-time scanning through a shared anode resistor would.
 *
 * A scan frame is 8 segment slots plus one dark slot in which the buttons
 * sharing segment lines (UP = F, DOWN = B, SET = A) are read, at 2 kHz -> 222 Hz.
 */
#ifndef DISPLAY_DISPLAY_H
#define DISPLAY_DISPLAY_H

#include <stdint.h>

#define BUTTON_RAW_ENTER    0x01
#define BUTTON_RAW_UP       0x02
#define BUTTON_RAW_DOWN     0x04
#define BUTTON_RAW_SET      0x08

void display_init(void);

/* Show a 3-byte frame (see font.h). Cheap; call whenever content changes. */
void display_write(const __xdata uint8_t *frame);

/* Buttons held down at the last scan (BUTTON_RAW_* bits). */
uint8_t display_buttons_raw(void);

/* Tick-interrupt hook: advance one scan slot. */
void display_scan(void);

#endif
