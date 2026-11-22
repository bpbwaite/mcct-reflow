/*
 * Temperature-voltage sampling. The tick interrupt converts AIN2 at 2 kHz and
 * sums one control period of samples (500 per 250 ms = exactly 15 cycles of
 * 60 Hz, so mains hum averages out). The main loop takes finished windows.
 */
#ifndef SENSOR_ADC_H
#define SENSOR_ADC_H

#include "common/config.h"
#include "board/tick.h"

#define ADC_SAMPLES_PER_WINDOW  ((uint16_t)((uint32_t)TICK_HZ * CFG_CONTROL_PERIOD_MS / 1000))

void adc_init(void);

/* True when a window has completed since the last adc_take_reading(). */
bool adc_window_ready(void);

/* The window's average as a Q16 fraction of VDD (0 .. 65520): the ADC is
 * ratiometric, so this is the sensor voltage / VDD. */
uint16_t adc_take_reading(void);

/* Tick-interrupt hook. */
void adc_scan(void);

#endif
