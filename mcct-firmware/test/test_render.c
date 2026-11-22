#include "test.h"
#include "display/render.h"

static bool frame(uint8_t a, uint8_t b, uint8_t c)
{
    return render_frame[0] == a && render_frame[1] == b && render_frame[2] == c;
}

void test_render(void)
{
    render_int(150);
    CHECK(frame(font_glyph('1'), font_glyph('5'), font_glyph('0')));
    render_int(7);
    CHECK(frame(0, 0, font_glyph('7')));
    render_int(-5);
    CHECK(frame(0, font_glyph('-'), font_glyph('5')));
    render_int(-99);
    CHECK(frame(font_glyph('-'), font_glyph('9'), font_glyph('9')));
    render_int(1000);
    CHECK(frame(SEG_G, SEG_G, SEG_G));                     /* "---" */

    render_tenths(290);                                     /* 29.0 */
    CHECK(frame(font_glyph('2'), font_glyph('9') | SEG_DP, font_glyph('0')));
    render_tenths(5);                                       /*  0.5 */
    CHECK(frame(0, font_glyph('0') | SEG_DP, font_glyph('5')));
    render_tenths(1234);                                    /* 123 */
    CHECK(frame(font_glyph('1'), font_glyph('2'), font_glyph('3')));

    render_text("1.5");
    CHECK(frame(font_glyph('1') | SEG_DP, font_glyph('5'), 0));
    render_text("[=]");
    CHECK(frame(0x39, 0x48, 0x0F));
    render_text("uuu");
    CHECK(frame(0x1C, 0x1C, 0x1C));
    CHECK_EQ(font_glyph('#'), 0);                           /* unknown: blank */
}
