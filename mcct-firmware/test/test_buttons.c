#include "test.h"
#include "input/buttons.h"
#include "display/display.h"
#include "common/config.h"

static __xdata uint16_t ups, longs, shorts;

/* Hold `raw` for `duration` ms, counting events. */
static void run(uint8_t raw, uint16_t duration) __reentrant
{
    static __xdata uint16_t t;
    btn_event_t ev;

    for (t = 0; t < duration; t += CFG_BUTTON_POLL_MS) {
        ev = buttons_poll(raw);
        if (ev == BTN_UP) ups++;
        if (ev == BTN_ENTER) shorts++;
        if (ev == BTN_SET) longs++;
    }
}

static void reset_counts(void)
{
    ups = shorts = longs = 0;
}

void test_buttons(void)
{
    static __xdata uint16_t slow, fast;
    static __xdata uint8_t i;

    buttons_init();

    /* debounce: a press shorter than CFG_DEBOUNCE_MS is ignored */
    reset_counts();
    run(BUTTON_RAW_UP, CFG_DEBOUNCE_MS - CFG_BUTTON_POLL_MS);
    run(0, 100);
    CHECK_EQ(ups, 0);

    /* chatter is ignored */
    for (i = 0; i < 20; i++) {
        run(BUTTON_RAW_UP, CFG_BUTTON_POLL_MS);
        run(0, CFG_BUTTON_POLL_MS);
    }
    CHECK_EQ(ups, 0);

    /* a clean press gives one event; holding repeats, faster and faster */
    run(BUTTON_RAW_UP, CFG_DEBOUNCE_MS);
    CHECK_EQ(ups, 1);
    run(BUTTON_RAW_UP, CFG_REPEAT_DELAY_MS - 2 * CFG_BUTTON_POLL_MS);
    CHECK_EQ(ups, 1);                       /* no repeat before the delay */
    reset_counts();
    run(BUTTON_RAW_UP, 1000);
    slow = ups;
    run(BUTTON_RAW_UP, 3000);
    reset_counts();
    run(BUTTON_RAW_UP, 1000);
    fast = ups;
    CHECK(slow >= 3 && slow <= 6);
    CHECK(fast >= 5 * slow);
    run(0, 100);

    /* ENTER: a short press reports on release */
    reset_counts();
    run(BUTTON_RAW_ENTER, 200);
    CHECK_EQ(shorts, 0);
    run(0, 100);
    CHECK_EQ(shorts, 1);
    CHECK_EQ(longs, 0);

    /* ENTER held: SET once and nothing on release if enabled, else ENTER on
     * release like a short press */
    reset_counts();
    run(BUTTON_RAW_ENTER, CFG_LONG_PRESS_MS + 500);
    CHECK_EQ(longs, CFG_ENTER_HOLD_IS_SET);
    CHECK_EQ(shorts, 0);
    run(0, 100);
    CHECK_EQ(shorts, !CFG_ENTER_HOLD_IS_SET);
    CHECK_EQ(longs, CFG_ENTER_HOLD_IS_SET);

    /* SET: one event on press, none while held or on release */
    reset_counts();
    run(BUTTON_RAW_SET, CFG_DEBOUNCE_MS - CFG_BUTTON_POLL_MS);
    run(0, 100);
    CHECK_EQ(longs, 0);
    run(BUTTON_RAW_SET, CFG_DEBOUNCE_MS);
    CHECK_EQ(longs, 1);
    run(BUTTON_RAW_SET, CFG_LONG_PRESS_MS + 500);
    run(0, 100);
    CHECK_EQ(longs, 1);
    CHECK_EQ(shorts, 0);

    /* injected events (debug console) come out on the next poll */
    buttons_inject(BTN_SET);
    CHECK_EQ(buttons_poll(0), BTN_SET);
    CHECK_EQ(buttons_poll(0), BTN_NONE);
}
