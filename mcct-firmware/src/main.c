/*
 * MCCT - 350 W soldering hot plate controller, Nuvoton MS51FB9AE.
 *
 * Architecture (each folder is one layer; logic layers never touch SFRs):
 *   board/    clock, pins, watchdog, 2 kHz tick interrupt
 *   display/  segment-multiplexed 7-segment driver, font, number rendering
 *   input/    button debounce, long press, accelerating auto-repeat
 *   sensor/   ADC window sampling, calibration polynomial
 *   control/  heater PDM, thermal model, PID, controller, reflow, auto-tune
 *   safety/   fault supervisor
 *   storage/  flash IAP, wear-levelled settings
 *   app/      process (what runs) and UI (what the user sees)
 *   debug/    UART console, telemetry, simulated hot plate
 *
 * Only display_scan() and adc_scan() run in interrupt context. Everything
 * else is a cooperative main loop: one control step per finished ADC window
 * (CFG_CONTROL_PERIOD_MS), a button/UI step every CFG_BUTTON_POLL_MS.
 */
#include "common/config.h"
#include "board/board.h"
#include "board/tick.h"             /* tick_isr() prototype: must be visible here */
#include "display/display.h"
#include "input/buttons.h"
#include "sensor/adc.h"
#include "sensor/temperature.h"
#include "control/heater.h"
#include "control/thermal_model.h"
#include "safety/safety.h"
#include "storage/settings.h"
#include "app/process.h"
#include "app/ui.h"
#include "debug/console.h"
#include "debug/cli.h"
#include "debug/plant_sim.h"

#define SAMPLE_TIMEOUT_MS   (4 * CFG_CONTROL_PERIOD_MS)
#define SIM_SENSOR_Q16      0x8000u     /* a plausible reading for the sensor check */

static __xdata struct {
    ms_t      last_sample;
    ms_t      last_poll;
    temp_dc_t pv;               /* last plate temperature, for the fault report */
    uint8_t   have_sample;
    uint8_t   sim;
} sys;

/* Heater off, "uuu" until the power is cycled. Never returns. */
static void fatal(fault_t fault)
{
    heater_off_now();
    proc_off();
    ui_fatal();
    cli_report("FATAL ", fault);
    cli_report("pv ", sys.pv);
    for (;;) {
        board_watchdog_feed();
        heater_off_now();
        ui_render();
    }
}

#if CFG_PLANT_SIM
/* Swap between the real sensor/heater and the simulated plate. */
static void toggle_sim(void)
{
    proc_off();
    sys.sim = !sys.sim;
    heater_enable_output(!sys.sim);
    sim_reset(model_ambient());
    safety_reset();
    cli_report("sim ", sys.sim);
}
#endif

static void control_step(void)
{
    static __xdata cli_sample_t s;
    fault_t fault;
    proc_event_t ev;

    s.now = clock_ms();
    s.sim = sys.sim;
    s.u_q16 = adc_take_reading();
#if CFG_PLANT_SIM
    if (sys.sim) {
        s.u_q16 = SIM_SENSOR_Q16;
        s.pv = sim_step(heater_duty());
    } else
#endif
        s.pv = temp_from_fraction(s.u_q16) + TEMP_DC(settings.offset_c);

    if (!sys.have_sample) {
        sys.have_sample = 1;
        if (s.pv <= TEMP_DC(CFG_AMBIENT_MAX_C))
            model_set_ambient(s.pv);    /* a cold plate tells us the room temperature */
    }

    sys.pv = s.pv;
    fault = safety_check(s.u_q16, s.pv, heater_duty());
    if (fault != FAULT_NONE)
        fatal(fault);

    ev = proc_step(s.pv);
    heater_command(proc_duty());
    ui_sample(s.pv);
    ui_process_event(ev);
    cli_telemetry(&s);
}

void main(void)
{
    reset_cause_t cause = board_init();     /* heater pin low first */
    ms_t now;

    board_watchdog_init();                  /* running since reset (CONFIG): set its period now */
    display_init();
    adc_init();
    buttons_init();
    console_init();
    safety_reset();
    settings_load();
    proc_apply_settings();
    model_set_ambient(TEMP_DC(CFG_AMBIENT_DEFAULT_C));     /* until the first reading */

    tick_init();                            /* interrupts on from here */

    now = clock_ms();
    heater_init();
    sys.last_sample = now;
    sys.last_poll = now;
    ui_init();
    cli_banner();

#if CFG_PLANT_SIM == 2
    toggle_sim();                           /* bench build: start simulated */
#endif
    if (cause == RESET_WATCHDOG)
        fatal(FAULT_WATCHDOG);
#if CFG_REQUIRE_HW_WATCHDOG
    if (!board_watchdog_from_config())
        fatal(FAULT_NO_WATCHDOG);
#endif

    for (;;) {
        board_watchdog_feed();
        now = clock_ms();

        if (adc_window_ready()) {
            sys.last_sample = now;
            control_step();
        } else if (MS_ELAPSED(now, sys.last_sample) > SAMPLE_TIMEOUT_MS) {
            fatal(FAULT_NO_SAMPLES);
        }

        if (MS_ELAPSED(now, sys.last_poll) >= CFG_BUTTON_POLL_MS) {
            sys.last_poll = now;
            ui_button(buttons_poll(display_buttons_raw()));
            ui_render();
        }

        heater_service();
        settings_service();
#if CFG_PLANT_SIM
        if (cli_service() == CLI_TOGGLE_SIM)
            toggle_sim();
#else
        cli_service();
#endif
    }
}
