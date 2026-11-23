#include "test.h"
#include "cal/cal_hold.h"
#include "common/config.h"

#define W(s) SECONDS_TO_STEPS(s)
#define FULL ((duty_t)CFG_CAL_MAX_DUTY_PERCENT * 100)

/* Lagged plate in 0.001 mV: ~0.137 mV/s per % (suspect model, 7 mV/degC),
 * 11 s heater lag, 600 s loss time constant. */
static __xdata int32_t mv3, q;

static __xdata duty_t duty;

static uint16_t plant(void)
{
    q += ((int32_t)duty * 10 - q) / 44;                     /* lagged duty, 0.001 % */
    mv3 += q * 137 / 4000 - (mv3 - 646000L) / 2400;         /* per 250 ms */
    return (uint16_t)(mv3 / 100);
}

static void set_target(uint16_t mv)
{
    hold_init();
    hold_adjust((int8_t)((mv - CFG_CAL_MIN_MV) / CFG_CAL_STEP_MV));
    CHECK_EQ(hold_set_mv, mv);
}

void test_cal_hold(void)
{
    static __xdata uint16_t i, mv, peak;
    static __xdata duty_t d, dmax;

    /* idle: no power */
    hold_init();
    CHECK_EQ(hold_update(6460), 0);
    CHECK_EQ(hold_state, HOLD_IDLE);

    /* the target is limited at both ends, below the ceiling */
    hold_adjust(100);
    CHECK_EQ(hold_set_mv, CFG_CAL_MAX_MV);
    CHECK(CFG_CAL_MAX_MV < CFG_CAL_CEILING_MV);
    hold_adjust(-100);
    CHECK_EQ(hold_set_mv, CFG_CAL_MIN_MV);

    /* ceiling and operator stop are latched until restarted */
    set_target(1000);
    hold_start(0, 6460);
    CHECK_EQ(hold_update(6460), FULL);
    CHECK_EQ(hold_update(CFG_CAL_CEILING_MV * 10u), 0);
    CHECK_EQ(hold_state, HOLD_CEILING);
    CHECK_EQ(hold_update(6460), 0);
    hold_start(0, 6460);
    hold_stop();
    CHECK_EQ(hold_update(6460), 0);
    CHECK_EQ(hold_state, HOLD_STOPPED);
    hold_adjust(1);                         /* neither of these restarts it */
    hold_keepalive();
    CHECK_EQ(hold_update(6460), 0);

    /* host lost */
    set_target(1000);
    hold_start(1, 9990);
    for (i = 0; i < W(CFG_CAL_HOST_S); i++)
        hold_update(9990);
    CHECK_EQ(hold_state, HOLD_RUNNING);
    hold_keepalive();
    for (i = 0; i < W(CFG_CAL_HOST_S); i++)
        hold_update(9990);
    CHECK_EQ(hold_state, HOLD_RUNNING);
    CHECK_EQ(hold_update(9990), 0);
    CHECK_EQ(hold_state, HOLD_HOST_LOST);

    /* time limit without an operator action */
    hold_start(0, 9990);
    for (i = 0; i < W(CFG_CAL_HOLD_MAX_S); i++)
        hold_update(9990);
    CHECK_EQ(hold_state, HOLD_RUNNING);
    CHECK_EQ(hold_update(9990), 0);
    CHECK_EQ(hold_state, HOLD_TIMEOUT);

    /* sensor does not follow the heat */
    set_target(1500);
    hold_start(0, 6460);
    for (i = 0; i < W(CFG_CAL_RISE_S); i++)
        d = hold_update(6460);
    CHECK_EQ(d, 0);
    CHECK_EQ(hold_state, HOLD_NO_RISE);

    /* closed loop from cold: capped power, small overshoot, settles */
    mv3 = 646000L;
    q = 0;
    mv = 6460;
    peak = 0;
    dmax = 0;
    hold_start(0, mv);
    for (i = 0; i < W(840); i++) {
        d = hold_update(mv);
        duty = d;
        mv = plant();
        if (mv > peak)
            peak = mv;
        if (d > dmax)
            dmax = d;
    }
    CHECK_EQ(hold_state, HOLD_RUNNING);
    CHECK_EQ(dmax, FULL);
    CHECK(peak < 15000 + 400);
    CHECK_NEAR(mv, 15000, 50);
    test_puts("    hold peak, end (0.1 mV): ");
    test_put_int(peak);
    test_puts(" ");
    test_put_int(mv);
    test_puts("\n");
}
