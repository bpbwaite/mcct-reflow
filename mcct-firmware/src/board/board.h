/* Board bring-up, reset cause, heater pin and watchdog. */
#ifndef BOARD_BOARD_H
#define BOARD_BOARD_H

#include "common/types.h"

#define FSYS_HZ     24000000UL

typedef enum {
    RESET_POWER_ON,     /* cold start */
    RESET_WATCHDOG,     /* the watchdog bit: something hung */
    RESET_OTHER         /* nRESET pin (bridge), brown-out or software */
} reset_cause_t;

/* First thing in main(): heater off, every pin in a safe mode, 24 MHz clock.
 * Returns why the chip reset and clears the flags. */
reset_cause_t board_init(void);

/* Heater optocoupler output (true = triac conducts). */
void board_heater_write(bool on);

/* Watchdog: set the period and feed it. The CONFIG bytes enable it
 * (config/chip_config.json); board_watchdog_from_config() reports whether
 * they do, so the firmware can refuse to run without it. */
void board_watchdog_init(void);
void board_watchdog_feed(void);
bool board_watchdog_from_config(void);

#endif
