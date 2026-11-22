/*
 * ADC reading -> plate temperature (pure).
 *
 * The sensor voltage is handled as a Q16 fraction of VDD (0 .. 65535 =
 * 0 .. VDD), which is what a ratiometric ADC measures. The calibration
 * polynomial T(u) is fitted at build time by scripts/gen_calibration.py
 * from calibration/adc_to_temperature.csv and evaluated here in fixed point.
 */
#ifndef SENSOR_TEMPERATURE_H
#define SENSOR_TEMPERATURE_H

#include "common/types.h"

/* Calibrated temperature for a Q16 sensor fraction (no user offset). */
temp_dc_t temp_from_fraction(uint16_t u_q16);

/* Q16 fraction -> millivolts at the pin, for telemetry. */
uint16_t temp_fraction_to_mv(uint16_t u_q16);

#endif
