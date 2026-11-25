/*
 * Console commands and telemetry for bench / hardware-in-the-loop work.
 *
 *   u d e E   press UP / DOWN / ENTER / SET
 *   t         telemetry on/off: one CSV line per control period
 *   s         settings: target,offset,profile,kp_x10,ti,td,loss_x1000,cap_x10,ambient
 *   S         plant simulator on/off (CFG_PLANT_SIM)
 *   W         hang the main loop to prove the watchdog (CFG_PLANT_SIM builds;
 *             the next boot must report FATAL 6)
 *   h ?       help
 *
 * Telemetry columns: ms, mv (sensor pin), pv, sp (0.1 degC), duty (0.01 %),
 * heater (0/1), mode (0 off 1 hold 2 reflow 3 tune), p, i, d (0.01 %).
 */
#ifndef DEBUG_CLI_H
#define DEBUG_CLI_H

#include "common/config.h"

typedef struct {
    ms_t      now;
    uint16_t  u_q16;
    temp_dc_t pv;
    uint8_t   sim;
} cli_sample_t;

typedef enum { CLI_NONE, CLI_TOGGLE_SIM } cli_action_t;

#if CFG_CONSOLE
void cli_banner(void);
cli_action_t cli_service(void);
void cli_telemetry(const cli_sample_t *s);
void cli_report(const char *what, int16_t value);   /* "what value" line */
#else
#define cli_banner()            ((void)0)
#define cli_service()           (CLI_NONE)
#define cli_telemetry(s)        ((void)0)
#define cli_report(w, v)        ((void)0)
#endif

#endif
