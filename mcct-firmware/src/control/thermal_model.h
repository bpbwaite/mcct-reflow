/*
 * Lumped thermal model of the plate (pure).
 *
 *   heater power = LOSS * (T - ambient) + CAP * dT/dt
 *
 * LOSS (thousandths of %/degC) is the power needed per degree above ambient
 * to hold a temperature; CAP (tenths of %*s/degC) is the energy per degree,
 * i.e. the plate's heat capacity relative to full heater power. The
 * auto-tuner measures both.
 *
 * The controller uses the model twice: as feed-forward (the PID only has to
 * correct the model's error, so it can be gentle) and to ramp setpoints no
 * faster than the plate can follow (no integrator wind-up, no overshoot).
 * Extension point: a better model (e.g. a second heater-element node or
 * radiation losses growing with T^4) only has to change this module.
 */
#ifndef CONTROL_THERMAL_MODEL_H
#define CONTROL_THERMAL_MODEL_H

#include "common/types.h"

/* loss_x1000: %/degC x 1000, cap_x10: %*s/degC x 10 */
void model_set_params(uint16_t loss_x1000, uint16_t cap_x10);
void model_set_ambient(temp_dc_t ambient);
temp_dc_t model_ambient(void);

/* Duty (0.01 %, unclamped) needed to hold `t` / to change at `rate`. */
int32_t model_hold_duty(temp_dc_t t);
int32_t model_ramp_duty(rate_cps_t rate);

/* Setpoint ramp limits at `t` (both >= 0, 0.01 degC/s): the fastest rise
 * the heater can drive, and the natural cooling rate, each scaled by
 * CFG_RAMP_HEADROOM_PERCENT so the plate can always keep up. */
rate_cps_t model_max_rise(temp_dc_t t);
rate_cps_t model_max_fall(temp_dc_t t);

#endif
