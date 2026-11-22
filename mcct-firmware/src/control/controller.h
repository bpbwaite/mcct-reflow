/*
 * Temperature controller (pure): turns a goal into a heater duty once per
 * control period, combining the setpoint trajectory, the thermal model's
 * feed-forward and the PID (gains: pid_set_gains()).
 *
 *   OFF     duty 0
 *   HOLD    constant temperature: the setpoint ramps from the current
 *           temperature to the target at the rate the model says the plate
 *           can follow (up: heater headroom, down: natural cooling)
 *   TRACK   follow a trajectory supplied every period (reflow profiles)
 *   MANUAL  fixed duty (auto-tuner relay)
 */
#ifndef CONTROL_CONTROLLER_H
#define CONTROL_CONTROLLER_H

#include "common/types.h"

typedef enum { CTRL_OFF, CTRL_HOLD, CTRL_TRACK, CTRL_MANUAL } ctrl_mode_t;

void ctrl_off(void);
/* HOLD and TRACK start from the last measurement passed to ctrl_update(). */
void ctrl_hold(temp_dc_t target);
void ctrl_track(temp_dc_t setpoint, rate_cps_t rate);
void ctrl_manual(duty_t duty);

duty_t ctrl_update(temp_dc_t pv);

temp_dc_t ctrl_setpoint(void);          /* current (ramped) setpoint */

#endif
