/*
 * PID auto-tuner and thermal-model identification (pure).
 *
 * Relay method (Astrom-Hagglund): the heater switches between bias+d and
 * bias-d whenever the plate crosses target -/+ hysteresis, which makes it
 * oscillate. The bias is re-centred every cycle so heating and cooling take
 * equally long (as in Marlin's tuner). From the oscillation:
 *
 *   ultimate gain    Ku = 4 d / (pi a)       a = half the peak-to-peak swing
 *   ultimate period  Pu = cycle length
 *   PID gains        Ziegler-Nichols rule from user_config.h
 *   model LOSS       average duty / (target - ambient)
 *   model CAP        from the full-power heating slope on the way up
 *                    (measured only when starting >= 40 degC below target)
 *
 * The first two cycles are discarded; the rest are averaged.
 */
#ifndef CONTROL_AUTOTUNE_H
#define CONTROL_AUTOTUNE_H

#include "common/types.h"

typedef enum { TUNE_RUNNING, TUNE_DONE, TUNE_FAILED } tune_status_t;

typedef struct {
    uint16_t kp_x10, ti_s, td_s;    /* PID gains (see pid.h) */
    uint16_t loss_x1000;            /* model LOSS, 0 = not measured */
    uint16_t cap_x10;               /* model CAP, 0 = not measured */
    uint16_t ku_x10;                /* ultimate gain, tenths of %/degC */
    uint16_t pu_s;                  /* ultimate period, s */
} tune_result_t;

extern __xdata tune_result_t tune_result;  /* valid after TUNE_DONE */

void tune_start(temp_dc_t target, temp_dc_t pv, temp_dc_t ambient);
tune_status_t tune_step(temp_dc_t pv);
duty_t tune_duty(void);

#endif
