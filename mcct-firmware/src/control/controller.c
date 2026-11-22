#include "control/controller.h"
#include "control/pid.h"
#include "control/thermal_model.h"
#include "common/config.h"
#include "common/fixed.h"

/* Never ramp slower than this, so a pessimistic model cannot stall the
 * setpoint short of the target (the PID then simply saturates). */
#define MIN_RAMP_CPS    10

static __xdata struct {
    ctrl_mode_t mode;
    temp_dc_t   pv;             /* last measurement */
    temp_dc_t   target;
    int32_t     sp_cc;          /* setpoint, 0.01 degC (ramp resolution) */
    rate_cps_t  rate;           /* setpoint slope, for feed-forward */
    duty_t      manual;
} c;

/* Restart the PID unless we are already closing the loop. */
static void enter_closed_loop(ctrl_mode_t mode)
{
    if (c.mode != CTRL_HOLD && c.mode != CTRL_TRACK) {
        pid_reset();
        c.sp_cc = (int32_t)c.pv * 10;
        c.rate = 0;
    }
    c.mode = mode;
}

void ctrl_off(void)
{
    c.mode = CTRL_OFF;
}

void ctrl_hold(temp_dc_t target)
{
    enter_closed_loop(CTRL_HOLD);
    c.target = target;
}

void ctrl_track(temp_dc_t setpoint, rate_cps_t rate)
{
    enter_closed_loop(CTRL_TRACK);
    c.sp_cc = (int32_t)setpoint * 10;
    c.rate = rate;
}

void ctrl_manual(duty_t duty)
{
    c.mode = CTRL_MANUAL;
    c.manual = duty;
}

/* Move the HOLD setpoint one period towards the target, no faster than the
 * model says the plate can follow. */
static void ramp_setpoint(void)
{
    temp_dc_t sp = ctrl_setpoint();
    uint16_t up = (uint16_t)model_max_rise(sp);      /* both >= 0 */
    uint16_t down = (uint16_t)model_max_fall(sp);
    int32_t step = (int32_t)c.target * 10 - c.sp_cc;

    uint16_t ease;

    up = (up < MIN_RAMP_CPS ? MIN_RAMP_CPS : up) / CONTROL_STEPS_PER_S;
    down = (down < MIN_RAMP_CPS ? MIN_RAMP_CPS : down) / CONTROL_STEPS_PER_S;
    /* Ease in: never faster than covering the remaining distance in
     * CFG_RAMP_APPROACH_S (at least 0.01 degC per step, so it arrives). */
    ease = (uint16_t)((step < 0 ? -step : step) / SECONDS_TO_STEPS(CFG_RAMP_APPROACH_S)) + 1;
    if (up > ease)
        up = ease;
    if (down > ease)
        down = ease;
    if (step > (int32_t)up)
        step = up;
    else if (step < -(int32_t)down)
        step = -(int32_t)down;
    c.sp_cc += step;
    c.rate = (rate_cps_t)step * CONTROL_STEPS_PER_S;
}

duty_t ctrl_update(temp_dc_t pv)
{
    int32_t ff;

    c.pv = pv;
    switch (c.mode) {
    case CTRL_MANUAL:
        return c.manual;
    case CTRL_HOLD:
        ramp_setpoint();
        break;
    case CTRL_TRACK:
        break;
    default:
        return 0;
    }
    ff = model_hold_duty(ctrl_setpoint()) + model_ramp_duty(c.rate);
    return pid_update(ctrl_setpoint(), pv, (int16_t)fx_clamp(ff, 0, DUTY_FULL));
}

temp_dc_t ctrl_setpoint(void)
{
    return (temp_dc_t)(c.sp_cc / 10);
}
