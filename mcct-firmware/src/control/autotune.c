#include "control/autotune.h"
#include "common/config.h"
#include "common/fixed.h"

#define TIMEOUT_STEPS       SECONDS_TO_STEPS(CFG_TUNE_TIMEOUT_MIN * 60)
#define DISCARD_CYCLES      2
#define BIAS_MIN            (DUTY_FULL / 20)            /* 5 % */
#define BIAS_MAX            (DUTY_FULL - BIAS_MIN)
#define SLOPE_MIN_SPAN_DC   400                         /* need 40 degC of climb */
#define MIN_AMPLITUDE_DC    2
#define MIN_LOSS_SPAN_DC    100
#define NOT_REACHED         32767

_Static_assert(CFG_TUNE_CYCLES - DISCARD_CYCLES <= 6, "16-bit cycle sums");

__xdata tune_result_t tune_result;

static __xdata struct {
    temp_dc_t target, ambient;
    uint16_t  now;                  /* control steps since start */
    uint8_t   heating;
    uint8_t   cycles;
    duty_t    bias, d;
    uint16_t  t_on, t_off;          /* when the relay last switched on / off */
    uint16_t  t_high;               /* how long it stayed on */
    temp_dc_t tmax, tmin;           /* extremes within the current cycle */
    temp_dc_t lo_mark, hi_mark;     /* full-power heat-up slope ...           */
    uint16_t  t_lo, t_hi;           /* ... and when the plate passed the marks */
    /* sums over the kept cycles */
    uint8_t   samples;
    uint16_t  swing_sum;            /* peak-to-peak, 0.1 degC */
    uint16_t  d_sum;                /* relay amplitude, 0.01 % */
    uint16_t  period_sum;           /* steps */
    uint16_t  duty_sum;             /* average duty, 0.01 % */
} t;

void tune_start(temp_dc_t target, temp_dc_t pv, temp_dc_t ambient)
{
    temp_dc_t span = target - pv;

    t.target = target;
    t.ambient = ambient;
    t.now = 0;
    t.heating = 1;
    t.cycles = 0;
    t.bias = t.d = DUTY_FULL / 2;
    t.t_on = t.t_off = 0;
    t.tmax = t.tmin = pv;
    t.t_lo = t.t_hi = 0;
    t.lo_mark = t.hi_mark = NOT_REACHED;    /* no slope: CAP not measured */
    if (span >= SLOPE_MIN_SPAN_DC) {
        t.lo_mark = pv + span / 4;
        t.hi_mark = pv + span / 4 * 3;
    }
    t.samples = 0;
    t.swing_sum = t.d_sum = t.period_sum = t.duty_sum = 0;
}

/* value * num / den, limited to 1 .. 999 (the PID's range) */
static uint16_t gain(uint16_t value, uint8_t num, uint8_t den)
{
    uint32_t v = (uint32_t)value * num / den;
    return v > 999 ? 999 : (v ? (uint16_t)v : 1);
}

static void finish(void)
{
    uint16_t amplitude = t.swing_sum / 2 / t.samples;
    uint16_t d = t.d_sum / t.samples;
    uint16_t avg_duty = t.duty_sum / t.samples;
    uint16_t pu_s = t.period_sum / t.samples / CONTROL_STEPS_PER_S;
    int16_t span = t.target - t.ambient;
    int16_t mid;
    uint16_t rate;
    int32_t spare;

    if (amplitude < MIN_AMPLITUDE_DC)
        amplitude = MIN_AMPLITUDE_DC;
    /* Ku = 4 d / (pi a): (0.01 %) / (0.1 degC) -> tenths of %/degC */
    tune_result.ku_x10 = (uint16_t)(4000UL * d / (3142UL * amplitude));
    tune_result.pu_s = pu_s;
    tune_result.kp_x10 = gain(tune_result.ku_x10, CFG_TUNE_KP_NUM, CFG_TUNE_KP_DEN);
    tune_result.ti_s = gain(pu_s, CFG_TUNE_TI_NUM, CFG_TUNE_TI_DEN);
    tune_result.td_s = gain(pu_s, CFG_TUNE_TD_NUM, CFG_TUNE_TD_DEN);

    /* LOSS: duty needed per degree above ambient, thousandths of %/degC */
    tune_result.loss_x1000 = 0;
    if (span >= MIN_LOSS_SPAN_DC)
        tune_result.loss_x1000 = (uint16_t)((uint32_t)avg_duty * 100 / span);

    /* CAP: (full power - losses at the slope's midpoint) / heating rate */
    tune_result.cap_x10 = 0;
    if (t.t_lo && t.t_hi > t.t_lo && tune_result.loss_x1000) {
        rate = (uint16_t)((uint32_t)(t.hi_mark - t.lo_mark) * 10 * CONTROL_STEPS_PER_S / (t.t_hi - t.t_lo));
        mid = (t.lo_mark + t.hi_mark) / 2;
        spare = (int32_t)DUTY_FULL - (int32_t)tune_result.loss_x1000 * (mid - t.ambient) / 100;
        if (rate && spare > 0)
            tune_result.cap_x10 = (uint16_t)fx_clamp(spare * 10 / rate, 1, 65535);
    }
}

/* One full relay cycle (heat + cool) just ended at t.now. */
static void cycle_complete(void)
{
    uint16_t t_low = t.now - t.t_off;
    uint16_t period = t.t_high + t_low;

    if (t.cycles >= 1 && period) {
        if (t.cycles >= DISCARD_CYCLES) {
            t.swing_sum += t.tmax - t.tmin;
            t.d_sum += t.d;
            t.period_sum += period;
            t.duty_sum += (uint16_t)(((uint32_t)(t.bias + t.d) * t.t_high + (uint32_t)(t.bias - t.d) * t_low) / period);
            t.samples++;
        }
        /* Re-centre: more time heating than cooling means the bias is high. */
        t.bias = (duty_t)fx_clamp((int32_t)t.bias + (int32_t)t.d * ((int16_t)t.t_high - (int16_t)t_low) / period,
                                  BIAS_MIN, BIAS_MAX);
        t.d = t.bias > DUTY_FULL / 2 ? DUTY_FULL - t.bias : t.bias;
    }
    t.cycles++;
}

tune_status_t tune_step(temp_dc_t pv)
{
    t.now++;
    if (t.now > TIMEOUT_STEPS || pv > t.target + CFG_TUNE_MAX_OVERSHOOT_C * 10)
        return TUNE_FAILED;

    if (pv > t.tmax) t.tmax = pv;
    if (pv < t.tmin) t.tmin = pv;
    if (!t.t_lo && pv >= t.lo_mark) t.t_lo = t.now;
    if (t.t_lo && !t.t_hi && pv >= t.hi_mark) t.t_hi = t.now;

    if (t.heating && pv > t.target + CFG_TUNE_HYSTERESIS_DC) {
        t.heating = 0;
        t.t_off = t.now;
        t.t_high = t.now - t.t_on;
    } else if (!t.heating && pv < t.target - CFG_TUNE_HYSTERESIS_DC) {
        cycle_complete();
        t.heating = 1;
        t.t_on = t.now;
        t.tmax = t.tmin = pv;
        if (t.cycles >= CFG_TUNE_CYCLES) {
            finish();
            return TUNE_DONE;
        }
    }
    return TUNE_RUNNING;
}

duty_t tune_duty(void)
{
    return t.heating ? t.bias + t.d : t.bias - t.d;
}
