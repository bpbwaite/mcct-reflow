/*
 * Fixed-point units shared by every module.
 *
 * The MS51 has no FPU and SDCC's soft-float is large and slow, so all
 * physical quantities are scaled integers. Each quantity has exactly one
 * unit, named in its type, so a reader never has to guess the scale.
 */
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Temperature in tenths of a degree Celsius: 2345 = 234.5 degC. */
typedef int16_t temp_dc_t;
#define TEMP_DC(deg_c)          ((temp_dc_t)((deg_c) * 10))

/* Heater duty in hundredths of a percent: 0 .. DUTY_FULL (= 100.00 %). */
typedef uint16_t duty_t;
#define DUTY_FULL               10000u
#define DUTY_PERCENT(p)         ((duty_t)((p) * 100u))

/* Rate of temperature change in hundredths of a degree per second. */
typedef int16_t rate_cps_t;

/* Milliseconds from the 1 kHz system clock. Wraps every 65.5 s, so only
 * ever compare differences: MS_ELAPSED(now, then) >= period. */
typedef uint16_t ms_t;
#define MS_ELAPSED(now, since)  ((ms_t)((ms_t)(now) - (ms_t)(since)))

#define ARRAY_LEN(a)            (sizeof(a) / sizeof((a)[0]))

#endif
