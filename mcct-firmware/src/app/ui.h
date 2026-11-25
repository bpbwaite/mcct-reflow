/*
 * User interface state machine: four buttons, three digits.
 *
 *  boot      "[=]" splash, then STANDBY
 *  STANDBY   target flashes (solid while adjusting); UP/DOWN adjust; ENTER heats; SET = menu
 *  HEAT      plate temperature, steady; right DP = heater on; UP/DOWN adjust
 *            the target (shown for a moment); ENTER stops; SET = menu
 *  MENU      rFL (reflow) tun (auto-tune) P I d (PID) OFS (offset)
 *            rSt (factory reset); UP/DOWN browse, ENTER opens, SET = back
 *  RUN       reflow / auto-tune: temperature + decimal-point animation;
 *            display flashes twice at each reflow waypoint; any button cancels
 *  IDLE      above CFG_IDLE_TIMEOUT_C with no input for CFG_IDLE_TIMEOUT_S:
 *            heater off, idle animation until a button is pressed
 *  MESSAGE   "End" / "Err" after a run, until a button is pressed
 *  FATAL     "uuu" flashing until power is cycled
 */
#ifndef APP_UI_H
#define APP_UI_H

#include "input/buttons.h"
#include "app/process.h"

void ui_init(void);
void ui_button(btn_event_t ev);
void ui_process_event(proc_event_t ev);
void ui_sample(temp_dc_t pv);               /* once per control period */
void ui_render(void);                       /* every button poll */
void ui_fatal(void);

#endif
