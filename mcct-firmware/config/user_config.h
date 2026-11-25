/*
 * MCCT hot plate - every user-configurable option lives in this file.
 *
 * Chip options (lock bit, hardware watchdog, brown-out) are NOT here: they are
 * programmed into the MS51's CONFIG bytes from config/chip_config.json.
 * Reflow curves are the JSON files in profiles/, the sensor calibration is
 * calibration/adc_to_temperature.csv; both are compiled in by build.ps1.
 *
 * Units: _C = whole degC, _DC = tenths of a degC, _MS / _S = time,
 * _PERCENT = percent of full heater power.
 */
#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* ---------------------------------------------------------------- mains */
#define CFG_MAINS_HZ                    60
/* The heater is switched by a zero-cross optotriac, so it is driven by
 * pulse-density modulation in whole "quanta" of this many mains cycles:
 * the shortest on (and off) time. 5 cycles at 60 Hz = 83 ms. */
#define CFG_HEATER_MIN_CYCLES           5
/* The heater is forced off if the control loop stops refreshing it. */
#define CFG_HEATER_DEADMAN_MS           1000

/* --------------------------------------------------------- temperatures */
#define CFG_TARGET_MIN_C                30
#define CFG_TARGET_MAX_C                250     /* also caps reflow profiles (owner: low-temp paste only) */
#define CFG_TARGET_DEFAULT_C            150
#define CFG_OFFSET_LIMIT_C              25      /* user offset range: +/- this */
/* At boot a reading at or below CFG_AMBIENT_MAX_C is taken as the ambient
 * (room) temperature for the thermal model; otherwise the default is used. */
#define CFG_AMBIENT_DEFAULT_C           25
#define CFG_AMBIENT_MAX_C               40

/* --------------------------------------------------------------- safety */
/* Any of these latches the "uuu" fatal screen until power is cycled. */
#define CFG_FATAL_OVERTEMP_C            280
/* Raw sensor voltage outside this window (per mille of VDD) means the
 * sensor or its amplifier is open, shorted or unpowered. The sensor is a
 * compensated thermocouple amplifier that may read only a few hundred mV in a
 * cold room, so the lower limit is 0 (=> no low-rail check). */
#define CFG_SENSOR_MIN_PERMILLE         0
#define CFG_SENSOR_MAX_PERMILLE         995
/* Heating timeout: while the heater runs at or above this duty, the
 * temperature must rise by CFG_RUNAWAY_MIN_RISE_C every CFG_RUNAWAY_WINDOW_S. */
#define CFG_RUNAWAY_DUTY_PERCENT        80
#define CFG_RUNAWAY_WINDOW_S            60
#define CFG_RUNAWAY_MIN_RISE_C          3
/* Stuck heater: once the heater has been commanded off for CFG_STUCK_SETTLE_S,
 * a rise of more than CFG_STUCK_MAX_RISE_C within CFG_STUCK_WINDOW_S means
 * the triac is conducting on its own (full power climbs ~60 degC/min). Kept
 * well above what a hot-air gun aimed at the plate can do. */
#define CFG_STUCK_SETTLE_S              90
#define CFG_STUCK_WINDOW_S              60
#define CFG_STUCK_MAX_RISE_C            20
/* Idle timeout: above this temperature, no button press for this long turns
 * the heater off and plays the idle animation until a button is pressed. */
#define CFG_IDLE_TIMEOUT_C              120
#define CFG_IDLE_TIMEOUT_S              600

/* -------------------------------------------------------------- control */
/* Sensor sampling and control period. Must divide 1000. */
#define CFG_CONTROL_PERIOD_MS           250
/* PID defaults, standard (ISA) form: u = Kp * (e + 1/Ti * integral(e) + Td * de/dt).
 * Kp in tenths of a percent of full power per degC, Ti and Td in seconds.
 * Ti = 0 disables the integral term. */
/* Tuned offline against a model fitted to logged runs, and checked against
 * plates 15-25 % off and lags up to ~11 s. */
#define CFG_PID_KP_X10_DEFAULT          30      /* 3.0 %/degC */
#define CFG_PID_TI_S_DEFAULT            800
#define CFG_PID_TD_S_DEFAULT            8
#define CFG_PID_I_ERROR_CLAMP_C         50      /* integrate at most this error */
#define CFG_PID_D_FILTER_SHIFT          3       /* derivative input low-pass, alpha = 1/2^n */
/* Thermal model defaults (replaced by the auto-tuner's measurements):
 *   holding power = LOSS * (T - ambient),  LOSS in thousandths of %/degC
 *   ramping power = CAP * dT/dt,           CAP in tenths of %*s/degC
 * Fitted to logged runs: 152 J/K, losses 15 W at 100 degC
 * and 55 W at 250 degC (convection + radiation) for the 350 W heater; LOSS
 * is the best straight line over 100..250 degC. */
#define CFG_MODEL_LOSS_X1000_DEFAULT    64
#define CFG_MODEL_CAP_X10_DEFAULT       434
/* The heater element's heat reaches the plate with this time constant
 * (never measured directly: about 4 s inferred from logged plateau steps, 11 s
 * from the first fit; the longer one overshoots least). Reflow ramps stop their feed-forward this long before
 * the ramp ends, so the heat still in the element finishes the ramp instead
 * of overshooting the waypoint. */
#define CFG_MODEL_LAG_S                 11
/* Constant-temperature mode ramps its setpoint at this share of the rate the
 * model says is achievable, so the plate arrives without overshoot. */
#define CFG_RAMP_HEADROOM_PERCENT       85
/* ... and eases into the target: the last part of the approach is an
 * exponential with this time constant, so the lagging plate does not coast
 * past the target when the ramp ends. */
#define CFG_RAMP_APPROACH_S             30

/* ------------------------------------------------------------ auto-tune */
#define CFG_TUNE_DEFAULT_C              200
#define CFG_TUNE_HYSTERESIS_DC          5       /* relay switches at target +/- 0.5 degC */
#define CFG_TUNE_CYCLES                 6       /* oscillations; the first two are discarded */
#define CFG_TUNE_TIMEOUT_MIN            45
#define CFG_TUNE_MAX_OVERSHOOT_C        25      /* abort if the plate runs this far past target */
/* Tuning rule (classic Ziegler-Nichols): Kp = Ku * 6/10, Ti = Pu / 2, Td = Pu / 8. */
#define CFG_TUNE_KP_NUM                 6
#define CFG_TUNE_KP_DEN                 10
#define CFG_TUNE_TI_NUM                 1
#define CFG_TUNE_TI_DEN                 2
#define CFG_TUNE_TD_NUM                 1
#define CFG_TUNE_TD_DEN                 8

/* --------------------------------------------------------------- reflow */
#define CFG_REFLOW_TOLERANCE_C          3       /* a waypoint counts as reached within this */
#define CFG_REFLOW_SETTLE_TIMEOUT_S     180     /* heating waypoint not reached: abort with "Err" */

/* ------------------------------------------------------- user interface */
#define CFG_SPLASH_MS                   1200    /* "[=]" at boot */
#define CFG_BLINK_PERIOD_MS             1000
#define CFG_BLINK_ON_MS                 600
#define CFG_PREVIEW_MS                  2000    /* target shown, solid, this long after an adjustment */
#define CFG_MENU_TIMEOUT_MS             20000
#define CFG_ANIMATION_STEP_MS           150
#define CFG_WAYPOINT_FLASH_MS           200     /* "flash twice": off/on/off/on, each this long */
#define CFG_SAVE_DELAY_MS               3000    /* settings are written this long after the last change */

/* -------------------------------------------------------------- buttons */
#define CFG_BUTTON_POLL_MS              10
#define CFG_DEBOUNCE_MS                 30
#define CFG_ENTER_HOLD_IS_SET           0       /* 1: holding ENTER also works as SET (menu / back) */
#define CFG_LONG_PRESS_MS               1000    /* ... when held this long */
/* UP/DOWN auto-repeat speeds up the longer they are held. */
#define CFG_REPEAT_DELAY_MS             500
#define CFG_REPEAT_SLOW_MS              200
#define CFG_REPEAT_MEDIUM_AFTER_MS      2000
#define CFG_REPEAT_MEDIUM_MS            80
#define CFG_REPEAT_FAST_AFTER_MS        4000
#define CFG_REPEAT_FAST_MS              30

/* ---------------------------------------------------------------- debug */
/* UART1 console on the ICP pins (the Arduino bridge passes it to USB).
 * CFG_CONSOLE_BAUD must equal PASSTHROUGH_BAUD in the bridge sketch. */
#define CFG_CONSOLE                     1
#define CFG_CONSOLE_BAUD                38400
#define CFG_VDD_MV                      5000    /* only used to print millivolts */
/* Simulated hot plate for bench / hardware-in-the-loop work without a heater
 * or sensor (debug/plant_sim.h). While it runs, the real heater output is
 * held off and the plate temperature comes from the model.
 *   0 = not compiled in (product build)
 *   1 = compiled in, toggled with console key 'S'
 *   2 = compiled in and running from power-up ('S' turns it off)
 * build.ps1 -Sim builds with 2 without editing this file. */
#ifndef CFG_PLANT_SIM
#define CFG_PLANT_SIM                   0
#endif
/* Calibration firmware (build.ps1 -Cal, src/cal/): holds a sensor voltage.
 * The ceiling is raw mV; 1850 mV is about 267 degC. */
#define CFG_CAL_MIN_MV                  700
#define CFG_CAL_MAX_MV                  1800    /* highest selectable hold */
#define CFG_CAL_CEILING_MV              1850    /* heater off at or above this */
#define CFG_CAL_STEP_MV                 100
#define CFG_CAL_MAX_DUTY_PERCENT        40
#define CFG_CAL_HOLD_MAX_S              900     /* heater off this long after the last target change */
#define CFG_CAL_HOST_S                  10      /* host-started hold: keep-alive timeout */
#define CFG_CAL_RISE_S                  60      /* at full allowed power the sensor must rise ... */
#define CFG_CAL_RISE_MV                 20      /* ... this much in that time */
/* Refuse to run (fatal) unless the CONFIG bytes enable the hardware watchdog. */
#define CFG_REQUIRE_HW_WATCHDOG         1

#endif
