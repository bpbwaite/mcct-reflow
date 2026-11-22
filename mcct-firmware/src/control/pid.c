#include "control/pid.h"
#include "common/config.h"
#include "common/fixed.h"

#define ERROR_LIMIT     3000                            /* 300 degC */
#define I_ERROR_LIMIT   (CFG_PID_I_ERROR_CLAMP_C * 10)
#define DE_LIMIT        (20 << 3)                       /* 2 degC per step */
#define TS_Q8           ((int32_t)CFG_CONTROL_PERIOD_MS * 256 / 1000)
#define INTEGRAL_LIMIT  ((int32_t)DUTY_FULL << 8)

__xdata pid_terms_t pid_terms;

static __xdata struct {
    uint16_t kp;                /* tenths of %/degC */
    uint16_t ti;                /* s */
    uint16_t kp_td_q4;          /* Kp * Td / 16 (derivative gain) */
    int16_t  e_filt_q3;         /* low-passed error, 0.1 degC << 3 */
} s;

static uint16_t limit_gain(uint16_t g)
{
    return g > PID_GAIN_MAX ? PID_GAIN_MAX : g;
}

void pid_set_gains(uint16_t kp_x10, uint16_t ti_s, uint16_t td_s)
{
    s.kp = limit_gain(kp_x10);
    s.ti = limit_gain(ti_s);
    s.kp_td_q4 = (uint16_t)(((uint32_t)s.kp * limit_gain(td_s)) >> 4);   /* <= 62375 */
}

void pid_reset(void)
{
    pid_terms.integral_q8 = 0;
    s.e_filt_q3 = 0;            /* loops start with the setpoint at the plate */
}

static int32_t output(int16_t feedforward)
{
    return feedforward + pid_terms.p + (pid_terms.integral_q8 >> 8) + pid_terms.d;
}

duty_t pid_update(temp_dc_t sp, temp_dc_t pv, int16_t feedforward)
{
    int16_t e, de;
    int32_t u, di;

    e = fx_clamp16(sp - pv, -ERROR_LIMIT, ERROR_LIMIT);

    /* P: (0.1 %/degC) * (0.1 degC) = 0.01 % */
    pid_terms.p = (int32_t)s.kp * e;

    /* D on the low-passed error: Kp * Td * de/dt
     *   = (kp * td / 16) * (de_q3 * 2) * steps_per_s
     * Setpoints here are always ramps, never steps, so this has no kick:
     * it pushes when a ramp starts and brakes when it ends, instead of
     * fighting P all the way up as D-on-measurement does. */
    de = ((e << 3) - s.e_filt_q3) >> CFG_PID_D_FILTER_SHIFT;
    s.e_filt_q3 += de;
    de = fx_clamp16(de, -DE_LIMIT, DE_LIMIT);
    pid_terms.d = (int32_t)s.kp_td_q4 * (de * (2 * CONTROL_STEPS_PER_S));

    /* I: Kp / Ti * e * Ts, with 8 fractional bits */
    if (s.ti) {
        di = (int32_t)s.kp * fx_clamp16(e, -I_ERROR_LIMIT, I_ERROR_LIMIT) * TS_Q8 / s.ti;
        u = output(feedforward);
        if ((u >= (int32_t)DUTY_FULL && di > 0) || (u <= 0 && di < 0))
            di = 0;                                     /* anti-windup */
        pid_terms.integral_q8 = fx_clamp(pid_terms.integral_q8 + di, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);
    } else {
        pid_terms.integral_q8 = 0;
    }

    u = output(feedforward);
    if (u <= 0)
        return 0;
    return u >= (int32_t)DUTY_FULL ? DUTY_FULL : (duty_t)u;
}
