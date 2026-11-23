#include "test.h"
#include "safety/safety.h"
#include "common/config.h"

#define U_OK    20000u          /* any plausible sensor reading */

void test_safety(void)
{
    static __xdata uint16_t i;
    static __xdata fault_t f;
    static __xdata temp_dc_t pv;

    safety_reset();
    CHECK_EQ(safety_check(U_OK, TEMP_DC(25), 0), FAULT_NONE);

    /* sensor at the top rail; 0 V is a legal reading unless a low limit is set */
#if CFG_SENSOR_MIN_PERMILLE > 0
    CHECK_EQ(safety_check(0, TEMP_DC(25), 0), FAULT_SENSOR);
#else
    CHECK_EQ(safety_check(0, TEMP_DC(25), 0), FAULT_NONE);
#endif
    CHECK_EQ(safety_check(65500u, TEMP_DC(25), 0), FAULT_SENSOR);

    /* over-temperature */
    CHECK_EQ(safety_check(U_OK, TEMP_DC(CFG_FATAL_OVERTEMP_C) - 1, 0), FAULT_NONE);
    CHECK_EQ(safety_check(U_OK, TEMP_DC(CFG_FATAL_OVERTEMP_C), 0), FAULT_OVERTEMP);

    /* heating at full power while the temperature stays put: heating timeout
     * after CFG_RUNAWAY_WINDOW_S, not before */
    safety_reset();
    f = FAULT_NONE;
    for (i = 0; i < SECONDS_TO_STEPS(CFG_RUNAWAY_WINDOW_S) && f == FAULT_NONE; i++)
        f = safety_check(U_OK, TEMP_DC(100), DUTY_FULL);
    CHECK_EQ(f, FAULT_NONE);
    CHECK_EQ(safety_check(U_OK, TEMP_DC(100), DUTY_FULL), FAULT_HEATING_TIMEOUT);

    /* ... but a plate that rises slowly (0.1 degC/s) is fine for a long time */
    safety_reset();
    f = FAULT_NONE;
    for (i = 0; i < 4000 && f == FAULT_NONE; i++)
        f = safety_check(U_OK, TEMP_DC(50) + (temp_dc_t)(i / 4), DUTY_FULL);
    CHECK_EQ(f, FAULT_NONE);

    /* heater commanded off but the plate climbs 24 degC/min: stuck triac */
    safety_reset();
    f = FAULT_NONE;
    pv = TEMP_DC(100);
    for (i = 0; i < 2000 && f == FAULT_NONE; i++, pv++)
        f = safety_check(U_OK, pv, 0);
    CHECK_EQ(f, FAULT_HEATER_STUCK);
    CHECK(i > SECONDS_TO_STEPS(CFG_STUCK_SETTLE_S));

    /* heater off and the plate cooling: never a fault */
    safety_reset();
    f = FAULT_NONE;
    pv = TEMP_DC(CFG_TARGET_MAX_C);
    for (i = 0; i < 4000 && f == FAULT_NONE; i++) {
        f = safety_check(U_OK, pv, 0);
        if ((i & 7) == 0)
            pv--;
    }
    CHECK_EQ(f, FAULT_NONE);
}
