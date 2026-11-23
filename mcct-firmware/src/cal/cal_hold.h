/*
 * Calibration hold: regulates the sensor *voltage*, so no temperature
 * calibration is involved (pure logic; unit-tested in test_cal_hold.c).
 *
 * Voltages are 0.1 mV of the nominal CFG_VDD_MV (as logged), duty is 0.01 %.
 * Call hold_update() once per CFG_CONTROL_PERIOD_MS. Every stop is latched
 * in hold_state until the next hold_start().
 */
#ifndef CAL_CAL_HOLD_H
#define CAL_CAL_HOLD_H

#include "common/types.h"

typedef enum {
    HOLD_IDLE,              /* 0 never started */
    HOLD_RUNNING,           /* 1 */
    HOLD_STOPPED,           /* 2 operator */
    HOLD_TIMEOUT,           /* 3 CFG_CAL_HOLD_MAX_S without an operator action */
    HOLD_CEILING,           /* 4 reading at or above CFG_CAL_CEILING_MV */
    HOLD_NO_RISE,           /* 5 heating hard but the sensor does not follow */
    HOLD_HOST_LOST          /* 6 host-started hold, no keep-alive */
} hold_state_t;

extern __xdata hold_state_t hold_state;
extern __xdata uint16_t hold_set_mv;        /* target, mV */

void hold_init(void);
void hold_adjust(int8_t steps);             /* CFG_CAL_STEP_MV each; restarts the time limit */
/* from_host: the host must then call hold_keepalive() every CFG_CAL_HOST_S */
void hold_start(uint8_t from_host, uint16_t mv_x10);
void hold_stop(void);
void hold_keepalive(void);
duty_t hold_update(uint16_t mv_x10);        /* 0 unless running */

#endif
