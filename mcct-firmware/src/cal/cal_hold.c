#include "cal/cal_hold.h"
#include "common/config.h"

#define KP              4           /* 0.01 % per 0.1 mV = 0.4 %/mV */
#define I_DIV           300         /* Ti = 300 s: Kp / (Ti * 4 steps/s) = 1/300 */
#define I_ZONE          300         /* integrate only within 30 mV */
#define DUTY_MAX        ((int32_t)CFG_CAL_MAX_DUTY_PERCENT * 100)
#define I_MAX           (DUTY_MAX * I_DIV)

__xdata hold_state_t hold_state;
__xdata uint16_t hold_set_mv;

static __xdata struct {
    int32_t  acc;
    uint16_t run_steps;         /* since the last operator action */
    uint16_t host_steps;        /* since the last keep-alive */
    uint16_t rise_steps;        /* windows at full allowed power */
    uint16_t rise_ref;          /* mv_x10 at the start of the rise check */
    uint8_t  host;
} h;

void hold_init(void)
{
    hold_state = HOLD_IDLE;
    hold_set_mv = CFG_CAL_MIN_MV;
}

void hold_adjust(int8_t steps)
{
    /* int16 is enough: 1800 + 127 * 100 */
    int16_t v = (int16_t)hold_set_mv + (int16_t)steps * CFG_CAL_STEP_MV;

    if (v < CFG_CAL_MIN_MV)
        v = CFG_CAL_MIN_MV;
    if (v > CFG_CAL_MAX_MV)
        v = CFG_CAL_MAX_MV;
    hold_set_mv = (uint16_t)v;
    h.run_steps = 0;
}

void hold_start(uint8_t from_host, uint16_t mv_x10)
{
    h.acc = 0;
    h.run_steps = 0;
    h.host_steps = 0;
    h.rise_steps = 0;
    h.rise_ref = mv_x10;
    h.host = from_host;
    hold_state = HOLD_RUNNING;
}

void hold_stop(void)
{
    if (hold_state == HOLD_RUNNING)
        hold_state = HOLD_STOPPED;
}

void hold_keepalive(void)
{
    h.host_steps = 0;
}

duty_t hold_update(uint16_t mv_x10)
{
    static __xdata int32_t e, u;            /* internal RAM is scarce */

    if (hold_state != HOLD_RUNNING)
        return 0;
    if (mv_x10 >= (uint16_t)(CFG_CAL_CEILING_MV * 10u)) {
        hold_state = HOLD_CEILING;
        return 0;
    }
    if (++h.run_steps > SECONDS_TO_STEPS(CFG_CAL_HOLD_MAX_S)) {
        hold_state = HOLD_TIMEOUT;
        return 0;
    }
    if (h.host && ++h.host_steps > SECONDS_TO_STEPS(CFG_CAL_HOST_S)) {
        hold_state = HOLD_HOST_LOST;
        return 0;
    }

    e = (int32_t)hold_set_mv * 10 - mv_x10;
    if (e < I_ZONE && e > -I_ZONE) {
        h.acc += e;
        if (h.acc < 0)
            h.acc = 0;
        if (h.acc > I_MAX)
            h.acc = I_MAX;
    }
    u = e * KP + h.acc / I_DIV;

    if (u < DUTY_MAX) {
        h.rise_steps = 0;
        h.rise_ref = mv_x10;
        return u <= 0 ? 0 : (duty_t)u;
    }
    /* Full allowed power must move the sensor: catches a detached or open
     * thermocouple that just reads cold. */
    if (++h.rise_steps >= SECONDS_TO_STEPS(CFG_CAL_RISE_S)) {
        if (mv_x10 < h.rise_ref + CFG_CAL_RISE_MV * 10u) {
            hold_state = HOLD_NO_RISE;
            return 0;
        }
        h.rise_steps = 0;
        h.rise_ref = mv_x10;
    }
    return (duty_t)DUTY_MAX;
}
