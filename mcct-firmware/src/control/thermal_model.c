#include "control/thermal_model.h"
#include "common/config.h"
#include "common/fixed.h"

#define RATE_LIMIT  3000        /* 30 degC/s: far beyond any real plate */

/* Set at boot by proc_apply_settings() and main(). (No initialisers: see
 * "XDATA initialisers" in AGENTS.md.) */
static __xdata struct {
    uint16_t loss_x1000;
    uint16_t cap_x10;
} params;
static __xdata temp_dc_t ambient;

void model_set_params(uint16_t loss_x1000, uint16_t cap_x10)
{
    params.loss_x1000 = loss_x1000;
    params.cap_x10 = cap_x10 ? cap_x10 : 1;
}

void model_set_ambient(temp_dc_t t)
{
    ambient = t;
}

temp_dc_t model_ambient(void)
{
    return ambient;
}

int32_t model_hold_duty(temp_dc_t t)
{
    /* (%/degC x 1000) * (0.1 degC) / 100 = 0.01 % */
    return (int32_t)params.loss_x1000 * (t - ambient) / 100;
}

int32_t model_ramp_duty(rate_cps_t rate)
{
    /* (%*s/degC x 10) * (0.01 degC/s) / 10 = 0.01 % */
    return (int32_t)params.cap_x10 * rate / 10;
}

static rate_cps_t duty_to_rate(int32_t duty)
{
    if (duty <= 0)
        return 0;
    return (rate_cps_t)fx_clamp(duty * 10 / params.cap_x10, 0, RATE_LIMIT);
}

rate_cps_t model_max_rise(temp_dc_t t)
{
    return duty_to_rate((int32_t)DUTY_FULL * CFG_RAMP_HEADROOM_PERCENT / 100 - model_hold_duty(t));
}

rate_cps_t model_max_fall(temp_dc_t t)
{
    return duty_to_rate(model_hold_duty(t) * CFG_RAMP_HEADROOM_PERCENT / 100);
}
