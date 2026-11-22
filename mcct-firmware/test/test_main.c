/* Test runner: prints through ucsim's simulator interface, then stops it. */
#include "test.h"

/* ucsim -I if=xram[0xffff]: write a command byte, then its argument. */
static volatile __xdata __at(0xFFFF) uint8_t sim_if;
#define SIF_PRINT   'p'
#define SIF_STOP    's'

__xdata uint16_t test_checks;
static __xdata uint16_t failures;

void test_puts(const char *s)
{
    while (*s) {
        sim_if = SIF_PRINT;
        sim_if = *s++;
    }
}

void test_put_int(int32_t v)
{
    static __xdata char buf[11];
    static __xdata uint8_t n;
    static __xdata uint32_t u;

    n = 0;
    u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        test_puts("-");
    do {
        buf[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (n) {
        sim_if = SIF_PRINT;
        sim_if = buf[--n];
    }
}

__xdata test_args_t test_args;

void test_check(uint16_t line)
{
    test_checks++;
    if (test_args.got - test_args.want <= (int32_t)test_args.tolerance
        && test_args.want - test_args.got <= (int32_t)test_args.tolerance)
        return;
    failures++;
    test_puts("FAIL ");
    test_puts(test_args.file);
    test_puts(":");
    test_put_int(line);
    test_puts(": ");
    test_puts(test_args.what);
    test_puts("  got ");
    test_put_int(test_args.got);
    test_puts(" want ");
    test_put_int(test_args.want);
    test_puts("\n");
}

#define SUITE(name) do { extern void name(void); test_puts("  " #name "\n"); name(); } while (0)

void main(void)
{
    test_puts("unit tests (8051 simulator)\n");
    SUITE(test_fixed);
    SUITE(test_temperature);
    SUITE(test_render);
    SUITE(test_buttons);
    SUITE(test_heater);
    SUITE(test_pid);
    SUITE(test_controller);
    SUITE(test_reflow);
    SUITE(test_safety);
    SUITE(test_settings);
    SUITE(test_closed_loop);
    SUITE(test_autotune);
    SUITE(test_ui);

    test_put_int(test_checks);
    test_puts(" checks, ");
    test_put_int(failures);
    test_puts(" failures\n");
    if (!failures)
        test_puts("ALL TESTS PASSED\n");
    sim_if = SIF_STOP;
    for (;;)
        ;
}
