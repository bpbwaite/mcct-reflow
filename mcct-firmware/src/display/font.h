/*
 * 7-segment glyphs. A frame is 3 bytes, digit 1 (left) first; each byte has
 * one bit per segment:
 *
 *      --A--
 *     F     B
 *      --G--
 *     E     C
 *      --D--  .DP
 */
#ifndef DISPLAY_FONT_H
#define DISPLAY_FONT_H

#include <stdint.h>

#define SEG_A   0x01
#define SEG_B   0x02
#define SEG_C   0x04
#define SEG_D   0x08
#define SEG_E   0x10
#define SEG_F   0x20
#define SEG_G   0x40
#define SEG_DP  0x80

#define DISPLAY_DIGITS  3

/* Segment pattern for a character (digits and the GLYPH table in font.c,
 * case-sensitive); unknown characters are blank. */
uint8_t font_glyph(char c);

#endif
