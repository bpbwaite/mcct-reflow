#include "safety/safety.h"
#include "common/config.h"

#define SENSOR_MIN_Q16      ((uint16_t)(65536UL * CFG_SENSOR_MIN_PERMILLE / 1000))
#define SENSOR_MAX_Q16      ((uint16_t)(65536UL * CFG_SENSOR_MAX_PERMILLE / 1000))
#define RUNAWAY_DUTY        DUTY_PERCENT(CFG_RUNAWAY_DUTY_PERCENT)
#define RUNAWAY_STEPS       SECONDS_TO_STEPS(CFG_RUNAWAY_WINDOW_S)
#define STUCK_SETTLE_STEPS  SECONDS_TO_STEPS(CFG_STUCK_SETTLE_S)
#define STUCK_WINDOW_STEPS  SECONDS_TO_STEPS(CFG_STUCK_WINDOW_S)

static __xdata struct {
    /* heating timeout: window of hard heating */
    uint16_t  hot_steps;
    temp_dc_t hot_start;
    /* stuck heater: time since the heater was last commanded on */
    uint16_t  off_steps;
    temp_dc_t off_ref;
} s;

void safety_reset(void)
{
    s.hot_steps = 0;
    s.off_steps = 0;
}

static fault_t check_heating(temp_dc_t pv, duty_t duty)
{
    if (duty < RUNAWAY_DUTY) {
        s.hot_steps = 0;
        return FAULT_NONE;
    }
    if (s.hot_steps == 0 || pv >= s.hot_start + CFG_RUNAWAY_MIN_RISE_C * 10) {
        s.hot_start = pv;               /* (re)start the window */
        s.hot_steps = 1;
        return FAULT_NONE;
    }
    if (++s.hot_steps > RUNAWAY_STEPS)
        return FAULT_HEATING_TIMEOUT;
    return FAULT_NONE;
}

static fault_t check_stuck(temp_dc_t pv)
{
    if (s.off_steps < STUCK_SETTLE_STEPS) {
        s.off_steps++;
        s.off_ref = pv;
        return FAULT_NONE;
    }
    if (pv > s.off_ref + CFG_STUCK_MAX_RISE_C * 10)
        return FAULT_HEATER_STUCK;
    if (pv < s.off_ref)
        s.off_ref = pv;                 /* track the coolest point */
    if (++s.off_steps >= STUCK_SETTLE_STEPS + STUCK_WINDOW_STEPS) {
        s.off_steps = STUCK_SETTLE_STEPS;   /* next window */
        s.off_ref = pv;
    }
    return FAULT_NONE;
}

fault_t safety_check(uint16_t u_q16, temp_dc_t pv, duty_t duty)
{
    if (u_q16 < SENSOR_MIN_Q16 || u_q16 > SENSOR_MAX_Q16)
        return FAULT_SENSOR;
    if (pv >= CFG_FATAL_OVERTEMP_C * 10)
        return FAULT_OVERTEMP;
    if (duty) {
        s.off_steps = 0;
        return check_heating(pv, duty);
    }
    s.hot_steps = 0;
    return check_stuck(pv);
}
