/*
 * Include this, not user_config.h directly: it adds the values derived from
 * the user options and checks them at compile time.
 */
#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include "user_config.h"
#include "common/types.h"

/* Control steps per second, and a duration in seconds as a step count. */
#define CONTROL_STEPS_PER_S     (1000 / CFG_CONTROL_PERIOD_MS)
#define SECONDS_TO_STEPS(s)     ((uint16_t)((s) * CONTROL_STEPS_PER_S))

/* Heater PDM quantum in ms (5 cycles at 60 Hz = 83 ms). */
#define HEATER_QUANTUM_MS       ((CFG_HEATER_MIN_CYCLES * 1000 + CFG_MAINS_HZ / 2) / CFG_MAINS_HZ)

_Static_assert(1000 % CFG_CONTROL_PERIOD_MS == 0, "CFG_CONTROL_PERIOD_MS must divide 1000");
_Static_assert(CFG_CONTROL_PERIOD_MS >= 100 && CFG_CONTROL_PERIOD_MS <= 1000, "control period 100..1000 ms");
_Static_assert(CFG_TARGET_MIN_C < CFG_TARGET_MAX_C, "target range");
_Static_assert(CFG_TARGET_MAX_C < CFG_FATAL_OVERTEMP_C, "max target must be below the fatal limit");
_Static_assert(CFG_TARGET_DEFAULT_C >= CFG_TARGET_MIN_C && CFG_TARGET_DEFAULT_C <= CFG_TARGET_MAX_C, "default target");
_Static_assert(CFG_TUNE_DEFAULT_C >= CFG_TARGET_MIN_C && CFG_TUNE_DEFAULT_C <= CFG_TARGET_MAX_C, "default tune target");
_Static_assert(CFG_TUNE_CYCLES >= 3, "auto-tune needs at least 3 cycles");
_Static_assert(CFG_DEBOUNCE_MS >= CFG_BUTTON_POLL_MS, "debounce must be at least one poll");
_Static_assert(CFG_IDLE_TIMEOUT_S <= 16000, "idle timeout must fit the step counter");
_Static_assert(CFG_MENU_TIMEOUT_MS < 60000, "ms timers wrap at 65 s");
_Static_assert(CFG_SAVE_DELAY_MS < 60000, "ms timers wrap at 65 s");

#endif
