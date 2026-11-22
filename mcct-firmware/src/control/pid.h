/*
 * PID controller, standard (ISA) form, fixed point (pure):
 *
 *   u = FF + Kp * ( e + 1/Ti * integral(e dt) + Td * de/dt )
 *
 * - Feed-forward FF comes from the thermal model.
 * - Derivative acts on the low-pass-filtered error. Every setpoint is a
 *   ramp (the controller and the reflow sequencer never step it), so there
 *   is no derivative kick, and D brakes at the end of a ramp instead of
 *   fighting P during it.
 * - Anti-windup: the integral is frozen while the output is saturated in
 *   the direction it would push, and bounded to +/- full scale.
 * Called once per CFG_CONTROL_PERIOD_MS. There is one controller, so the
 * state is module-private (no pointers: cheap on the 8051).
 */
#ifndef CONTROL_PID_H
#define CONTROL_PID_H

#include "common/types.h"

#define PID_GAIN_MAX    999

/* Kp in tenths of %/degC, Ti and Td in seconds (Ti = 0: no integral).
 * Values above PID_GAIN_MAX are limited. */
void pid_set_gains(uint16_t kp_x10, uint16_t ti_s, uint16_t td_s);
void pid_reset(void);
duty_t pid_update(temp_dc_t sp, temp_dc_t pv, int16_t feedforward);

/* Last terms in 0.01 % (read-only, for telemetry). */
typedef struct {
    int32_t p;
    int32_t integral_q8;        /* I term << 8 */
    int32_t d;
} pid_terms_t;
extern __xdata pid_terms_t pid_terms;

#endif
