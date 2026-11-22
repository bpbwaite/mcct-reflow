/*
 * Building the display frame (pure). The functions draw into one shared
 * 3-byte frame, digit 1 (left) first, which the UI then hands to
 * display_write(). (One buffer and no pointer arguments: on the 8051 this
 * is much smaller than passing frames around.)
 */
#ifndef DISPLAY_RENDER_H
#define DISPLAY_RENDER_H

#include "display/font.h"
#include "common/types.h"

extern __xdata uint8_t render_frame[DISPLAY_DIGITS];

void render_blank(void);

/* Left-aligned text, up to 3 characters; a '.' lights the decimal point of
 * the preceding character ("1.5" fits in two digits). */
void render_text(const char *text);

/* Right-aligned integer, -99 .. 999; anything else shows "---". */
void render_int(int16_t value);

/* Value in tenths, shown with one decimal ("29.0") when below 100.0,
 * otherwise as a rounded integer. */
void render_tenths(int16_t tenths);

/* Light the decimal point of digit 0..2. */
void render_dp(uint8_t digit);

#endif
