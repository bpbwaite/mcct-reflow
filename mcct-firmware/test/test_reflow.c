#include "test.h"
#include "control/reflow.h"
#include "common/config.h"

void test_reflow(void)
{
    static __xdata uint16_t steps, expected;
    static __xdata uint8_t waypoints, i;
    static __xdata reflow_event_t ev;
    static __xdata temp_dc_t pv;

    /* An ideal plate that follows the setpoint exactly: every waypoint is
     * reached, then the profile ends. */
    reflow_start(0, TEMP_DC(25));
    pv = TEMP_DC(25);
    waypoints = 0;
    for (steps = 0; steps < 20000; steps++) {
        ev = reflow_step(pv);
        pv = reflow_setpoint();
        if (ev == REFLOW_EV_WAYPOINT)
            waypoints++;
        if (ev == REFLOW_EV_DONE || ev == REFLOW_EV_FAILED)
            break;
    }
    CHECK_EQ(ev, REFLOW_EV_DONE);
    CHECK_EQ(waypoints, profiles[0].count);
    expected = 0;
    for (i = 0; i < profiles[0].count; i++)
        expected += SECONDS_TO_STEPS(profile_waypoints[i].ramp_s + profile_waypoints[i].hold_s);
    CHECK_NEAR(steps, expected, 2 * profiles[0].count + 2);

    /* the ramp feeds the controller a matching slope (0.01 degC/s) */
    reflow_start(0, TEMP_DC(25));
    reflow_step(TEMP_DC(25));
    CHECK_EQ(reflow_rate(), (profile_waypoints[0].temp - TEMP_DC(25)) * 10 / profile_waypoints[0].ramp_s);

    /* a plate that never heats: the first waypoint times out -> FAILED */
    reflow_start(0, TEMP_DC(25));
    for (steps = 0; steps < 20000; steps++) {
        ev = reflow_step(TEMP_DC(25));
        if (ev != REFLOW_EV_NONE)
            break;
    }
    CHECK_EQ(ev, REFLOW_EV_FAILED);
    CHECK_NEAR(steps, SECONDS_TO_STEPS(profile_waypoints[0].ramp_s + CFG_REFLOW_SETTLE_TIMEOUT_S), 2);
    CHECK_EQ(reflow_step(TEMP_DC(25)), REFLOW_EV_NONE);         /* stays finished */

    /* cooling waypoints wait as long as it takes (no timeout) */
    reflow_start(0, TEMP_DC(25));
    pv = TEMP_DC(25);
    for (steps = 0; steps < 30000; steps++) {
        ev = reflow_step(pv);
        if (reflow_setpoint() >= pv)
            pv = reflow_setpoint();             /* heats fine, never cools */
        if (ev == REFLOW_EV_DONE || ev == REFLOW_EV_FAILED)
            break;
    }
    CHECK_EQ(ev, REFLOW_EV_NONE);
}
