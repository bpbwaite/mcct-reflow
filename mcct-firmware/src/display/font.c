#include "display/font.h"

static const uint8_t __code digits[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

/* Character table. scripts/gen_profiles.py parses the GLYPH() lines to check
 * that reflow profile labels are displayable, so keep one entry per line. */
#define GLYPH(ch, segs)  { ch, segs }
static const struct { char ch; uint8_t segs; } __code glyphs[] = {
    GLYPH(' ', 0x00),
    GLYPH('-', 0x40),
    GLYPH('_', 0x08),
    GLYPH('=', 0x48),
    GLYPH('[', 0x39),
    GLYPH(']', 0x0F),
    GLYPH('A', 0x77),
    GLYPH('b', 0x7C),
    GLYPH('C', 0x39),
    GLYPH('c', 0x58),
    GLYPH('d', 0x5E),
    GLYPH('E', 0x79),
    GLYPH('F', 0x71),
    GLYPH('G', 0x3D),
    GLYPH('H', 0x76),
    GLYPH('h', 0x74),
    GLYPH('I', 0x30),
    GLYPH('i', 0x04),
    GLYPH('J', 0x1E),
    GLYPH('L', 0x38),
    GLYPH('n', 0x54),
    GLYPH('o', 0x5C),
    GLYPH('O', 0x3F),
    GLYPH('P', 0x73),
    GLYPH('q', 0x67),
    GLYPH('r', 0x50),
    GLYPH('S', 0x6D),
    GLYPH('t', 0x78),
    GLYPH('U', 0x3E),
    GLYPH('u', 0x1C),
    GLYPH('y', 0x6E),
};

uint8_t font_glyph(char c)
{
    uint8_t i;

    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    for (i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].ch == c)
            return glyphs[i].segs;
    return 0x00;
}
