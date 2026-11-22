/*
 * Reflow profile sequencer (pure). Produces a setpoint trajectory for the
 * controller's TRACK mode, one control period at a time.
 */
#ifndef CONTROL_REFLOW_H
#define CONTROL_REFLOW_H

#include "control/profile.h"

typedef enum {
    REFLOW_EV_NONE,
    REFLOW_EV_WAYPOINT,     /* the plate reached a waypoint */
    REFLOW_EV_DONE,         /* last waypoint held: profile complete */
    REFLOW_EV_FAILED        /* a heating waypoint was not reached in time */
} reflow_event_t;

void reflow_start(uint8_t profile, temp_dc_t pv);
reflow_event_t reflow_step(temp_dc_t pv);

temp_dc_t reflow_setpoint(void);
rate_cps_t reflow_rate(void);

#endif
