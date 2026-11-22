/*
 * Heater output: pulse-density modulation for a zero-cross optotriac.
 *
 * PWM makes no sense with a zero-cross switch (it can only turn on at a zero
 * crossing anyway), so time is divided into quanta of CFG_HEATER_MIN_CYCLES
 * mains cycles and a first-order sigma-delta decides, per quantum, whether
 * the heater conducts. Every on (and off) period is therefore at least one
 * quantum long and the average power matches the duty exactly.
 *
 * Safety: heater_command() must be called at least every
 * CFG_HEATER_DEADMAN_MS, otherwise the output drops to 0 on its own.
 */
#ifndef CONTROL_HEATER_H
#define CONTROL_HEATER_H

#include "common/types.h"

void heater_init(void);
void heater_command(duty_t duty);
void heater_service(void);              /* call from the main loop, often */
void heater_off_now(void);              /* immediate off (faults) */

/* Keep the real output pin off while the logic keeps running (plant
 * simulator). */
void heater_enable_output(bool enable);

bool heater_is_on(void);                /* logical state of the current quantum */
duty_t heater_duty(void);

#endif
