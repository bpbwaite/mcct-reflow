/*
 * What the machine is doing (pure): off, holding a temperature, running a
 * reflow profile or auto-tuning. Owns the controller and turns every sensor
 * sample into a heater duty. The UI starts and stops processes; this layer
 * reports their milestones back as events.
 */
#ifndef APP_PROCESS_H
#define APP_PROCESS_H

#include "common/types.h"

typedef enum { PROC_OFF, PROC_HOLD, PROC_REFLOW, PROC_TUNE } proc_mode_t;

typedef enum {
    PROC_EV_NONE,
    PROC_EV_WAYPOINT,       /* reflow waypoint reached */
    PROC_EV_DONE,           /* reflow finished / tuning finished (results saved) */
    PROC_EV_FAILED          /* reflow or tuning gave up */
} proc_event_t;

void proc_apply_settings(void);     /* (re)load gains and model from settings */

void proc_off(void);
void proc_hold(temp_dc_t target);   /* also changes the target while holding */
void proc_reflow(uint8_t profile);
void proc_tune(temp_dc_t target);

/* Once per control period with the plate temperature. */
proc_event_t proc_step(temp_dc_t pv);

proc_mode_t proc_mode(void);
duty_t proc_duty(void);
temp_dc_t proc_setpoint(void);

#endif
