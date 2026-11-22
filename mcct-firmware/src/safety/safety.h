/*
 * Safety supervisor (pure). Checked every control period, independently of
 * what the controller thinks it is doing. Any fault is fatal: the heater
 * goes off and the display flashes "uuu" until power is cycled.
 */
#ifndef SAFETY_SAFETY_H
#define SAFETY_SAFETY_H

#include "common/types.h"

/* The console prints faults by number: "FATAL n". */
typedef enum {
    FAULT_NONE = 0,
    FAULT_OVERTEMP,         /* 1: plate at/above CFG_FATAL_OVERTEMP_C */
    FAULT_SENSOR,           /* 2: reading at a rail: open/short/unpowered */
    FAULT_HEATING_TIMEOUT,  /* 3: heating hard but the temperature does not rise */
    FAULT_HEATER_STUCK,     /* 4: heater off but the temperature keeps rising */
    FAULT_NO_SAMPLES,       /* 5: the ADC stopped delivering readings */
    FAULT_WATCHDOG,         /* 6: reset by the watchdog: something hung */
    FAULT_NO_WATCHDOG,      /* 7: CONFIG bytes do not enable the watchdog */
    FAULT_COUNT
} fault_t;

void safety_reset(void);

/* u_q16: raw sensor fraction of VDD; pv: plate temperature; duty: what the
 * heater was commanded over the last period. */
fault_t safety_check(uint16_t u_q16, temp_dc_t pv, duty_t duty);

#endif
