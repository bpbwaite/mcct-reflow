/*
 * Simulated hot plate for hardware-in-the-loop testing without a heater or
 * sensor (pure). Two thermal masses: the heater element and the plate, with
 * a conductance between them, which gives the lag that makes real plates
 * overshoot. Enabled at run time from the console; while it runs, the real
 * heater output is held off and the plate temperature comes from here.
 *
 * Defaults: 350 W heater, 30 J/K element, 270 J/K plate, 5 W/K element->plate,
 * 0.45 W/K plate->ambient (matches the default thermal model).
 */
#ifndef DEBUG_PLANT_SIM_H
#define DEBUG_PLANT_SIM_H

#include "common/types.h"

void sim_reset(temp_dc_t ambient);
temp_dc_t sim_step(duty_t duty);        /* advance one period, return the reading */

#endif
