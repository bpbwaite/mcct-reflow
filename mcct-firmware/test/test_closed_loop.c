/*
 * Whole control stack against the simulated hot plate (debug/plant_sim.c):
 * process -> controller -> thermal model + PID, sensor noise included.
 * This is where the +/- 1 degC requirement and the auto-tuner are checked.
 */
#include "test.h"
#include "app/process.h"
#include "control/autotune.h"
#include "control/profile.h"
#include "control/thermal_model.h"
#include "debug/plant_sim.h"
#include "storage/settings.h"
#include "common/config.h"

#define MINUTES(m)  SECONDS_TO_STEPS((m) * 60)

static __xdata temp_dc_t pv, pv_max, band_min, band_max;
static __xdata proc_event_t last_event;

static void plant_start(void)
{
    sim_reset(TEMP_DC(25));
    model_set_ambient(TEMP_DC(25));
    pv = TEMP_DC(25);
    proc_off();
    proc_step(pv);
}

/* Run `steps` control periods; track the maximum, and the band within the
 * last `band_steps` of them. Stops early on a process event. */
static __xdata uint16_t band_from;

static uint16_t plant_run(uint16_t steps, uint16_t band_steps)
{
    static __xdata uint16_t i;

    band_from = steps - band_steps;
    pv_max = -32000;
    band_min = 32000;
    band_max = -32000;
    for (i = 0; i < steps; i++) {
        pv = sim_step(proc_duty());
        last_event = proc_step(pv);
        if (pv > pv_max)
            pv_max = pv;
        if (i >= band_from) {
            if (pv < band_min) band_min = pv;
            if (pv > band_max) band_max = pv;
        }
        if (last_event != PROC_EV_NONE && last_event != PROC_EV_WAYPOINT)
            break;
    }
    return i;
}

static void report(const char *what, int16_t v)
{
    test_puts("    ");
    test_puts(what);
    test_put_int(v);
    test_puts("\n");
}

static void hold_check(temp_dc_t target)
{
    bool heating_up = pv < target;

    proc_hold(target);
    plant_run(MINUTES(15), MINUTES(5));
    if (heating_up) {
        report("overshoot (0.1 degC): ", pv_max - target);
        CHECK(pv_max - target <= 10);               /* <= 1 degC overshoot */
    }
    report("band min (0.1 degC): ", band_min - target);
    report("band max (0.1 degC): ", band_max - target);
    CHECK(band_min >= target - 10);                 /* +/- 1 degC once settled */
    CHECK(band_max <= target + 10);
}

void test_closed_loop(void)
{
    settings_defaults();
    proc_apply_settings();
    plant_start();

    /* constant temperature, default gains and model */
    hold_check(TEMP_DC(200));

    /* step down to a lower target, then up to near the top of the range */
    hold_check(TEMP_DC(150));
    hold_check(TEMP_DC(300));

    /* a reflow profile on the simulated plate completes */
    proc_off();
    plant_run(MINUTES(40), 1);                      /* cool down */
    proc_reflow(1);
    plant_run(MINUTES(30), 1);
    CHECK_EQ(last_event, PROC_EV_DONE);
    CHECK(pv_max <= profile_waypoints[profiles[1].first + 2].temp + 50);
}

void test_autotune(void)
{
    static __xdata uint16_t steps;

    settings_defaults();
    proc_apply_settings();
    plant_start();

    proc_tune(TEMP_DC(200));
    steps = plant_run(MINUTES(CFG_TUNE_TIMEOUT_MIN) + 10, 1);
    CHECK_EQ(last_event, PROC_EV_DONE);
    report("tune time (s): ", steps / CONTROL_STEPS_PER_S);
    report("Ku x10: ", tune_result.ku_x10);
    report("Pu (s): ", tune_result.pu_s);
    report("Kp x10: ", settings.kp_x10);
    report("Ti: ", settings.ti_s);
    report("Td: ", settings.td_s);
    report("loss x1000: ", settings.loss_x1000);
    report("cap x10: ", settings.cap_x10);

    /* The simulated plate: 0.45 W/K loss and 300 J/K (plate + element) at
     * 3.5 W per % -> loss 0.129 %/degC, capacity ~86 %*s/degC. */
    CHECK_NEAR(settings.loss_x1000, 129, 26);
    CHECK_NEAR(settings.cap_x10, 860, 260);
    CHECK(settings.kp_x10 >= 20 && settings.kp_x10 <= 900);
    CHECK(settings.ti_s >= 5 && settings.ti_s <= 600);

    /* and the tuned controller holds the plate within +/- 1 degC */
    hold_check(TEMP_DC(200));
    hold_check(TEMP_DC(250));
}
