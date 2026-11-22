#include "display/render.h"
#include "common/fixed.h"

__xdata uint8_t render_frame[DISPLAY_DIGITS];

void render_blank(void)
{
    render_frame[0] = render_frame[1] = render_frame[2] = 0;
}

void render_dp(uint8_t digit)
{
    render_frame[digit] |= SEG_DP;
}

void render_text(const char *text)
{
    uint8_t pos = 0;

    render_blank();
    for (; *text; text++) {
        if (*text == '.' && pos > 0)
            render_frame[pos - 1] |= SEG_DP;
        else if (pos < DISPLAY_DIGITS)
            render_frame[pos++] = font_glyph(*text);
    }
}

void render_int(int16_t value)
{
    uint8_t pos = DISPLAY_DIGITS;
    uint16_t v;

    if (value > 999 || value < -99) {
        render_text("---");
        return;
    }
    render_blank();
    v = (value < 0) ? (uint16_t)(-value) : (uint16_t)value;
    do {
        render_frame[--pos] = font_glyph((char)('0' + v % 10));
        v /= 10;
    } while (v);
    if (value < 0)
        render_frame[--pos] = font_glyph('-');
}

void render_tenths(int16_t tenths)
{
    if (tenths >= 1000 || tenths <= -100) {
        render_int(temp_round_c(tenths));
        return;
    }
    render_int(tenths);
    if (tenths >= 0 && tenths < 10) {
        render_frame[1] = font_glyph('0');         /* 0.5, not " .5" */
    } else if (tenths < 0 && tenths > -10) {
        render_frame[0] = font_glyph('-');
        render_frame[1] = font_glyph('0');
    }
    render_dp(1);
}
