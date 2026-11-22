/*
 * Minimal unit-test framework for the 8051 simulator (ucsim).
 *
 * Tests are compiled by the same SDCC, memory model and options as the
 * firmware, so integer widths, overflow and fixed-point behaviour are
 * exactly what runs on the chip. Output and "stop" go through ucsim's
 * simulator interface (a magic XRAM byte); build.ps1 -Test runs them.
 */
#ifndef TEST_TEST_H
#define TEST_TEST_H

#include <stdint.h>
#include <stdbool.h>

void test_puts(const char *s);
void test_put_int(int32_t v);
extern __xdata uint16_t test_checks;

/* Checks pass their arguments in XRAM: internal RAM is too scarce on the
 * 8051 for a parameter block (or macro temporaries) per check. */
typedef struct {
    int32_t got, want;
    uint16_t tolerance;
    const char *file, *what;
} test_args_t;
extern __xdata test_args_t test_args;
void test_check(uint16_t line);

#define TEST_CHECK_(g, w, tol, text) do { test_args.got = (g); test_args.want = (w);     test_args.tolerance = (tol); test_args.file = __FILE__; test_args.what = (text);     test_check(__LINE__); } while (0)
#define CHECK(cond)                 TEST_CHECK_(!!(cond), 1, 0, #cond)
#define CHECK_EQ(got, want)         TEST_CHECK_((got), (want), 0, #got " == " #want)
#define CHECK_NEAR(got, want, tol)  TEST_CHECK_((got), (want), (tol), #got " ~ " #want)

/* ---- fakes (test/fakes.c) for the hardware the logic modules use ---- */
extern __xdata uint16_t fake_now;           /* clock_ms() */
extern __xdata uint8_t fake_heater_pin;     /* board_heater_write() */
extern __xdata uint8_t fake_frame[3];       /* display_write() */
void fake_flash_erase_all(void);
void fake_flash_poke(uint16_t addr, uint8_t value);

/* Frame helpers: compare the shown frame with rendered text. */
bool frame_is_text(const char *text);
bool frame_is_int(int16_t v);
bool frame_is_blank(void);

#endif
