#include "app/process.h"
#include "control/controller.h"
#include "control/pid.h"
#include "control/thermal_model.h"
#include "control/reflow.h"
#include "control/autotune.h"
#include "storage/settings.h"

static __xdata struct {
    proc_mode_t mode;
    temp_dc_t   pv;             /* last sample */
    duty_t      duty;
} p;

void proc_apply_settings(void)
{
    pid_set_gains(settings.kp_x10, settings.ti_s, settings.td_s);
    model_set_params(settings.loss_x1000, settings.cap_x10);
}

void proc_off(void)
{
    p.mode = PROC_OFF;
    ctrl_off();
}

void proc_hold(temp_dc_t target)
{
    p.mode = PROC_HOLD;
    ctrl_hold(target);
}

void proc_reflow(uint8_t profile)
{
    p.mode = PROC_REFLOW;
    reflow_start(profile, p.pv);
    ctrl_track(p.pv, 0);
}

void proc_tune(temp_dc_t target)
{
    p.mode = PROC_TUNE;
    tune_start(target, p.pv, model_ambient());
    ctrl_manual(tune_duty());
}

static void store_tune_results(void)
{
    settings.kp_x10 = tune_result.kp_x10;
    settings.ti_s = tune_result.ti_s;
    settings.td_s = tune_result.td_s;
    if (tune_result.loss_x1000)             /* 0 = could not be measured */
        settings.loss_x1000 = tune_result.loss_x1000;
    if (tune_result.cap_x10)
        settings.cap_x10 = tune_result.cap_x10;
    settings_changed();
    proc_apply_settings();
}

static proc_event_t step_reflow(temp_dc_t pv)
{
    switch (reflow_step(pv)) {
    case REFLOW_EV_WAYPOINT:
        return PROC_EV_WAYPOINT;
    case REFLOW_EV_DONE:
        proc_off();
        return PROC_EV_DONE;
    case REFLOW_EV_FAILED:
        proc_off();
        return PROC_EV_FAILED;
    default:
        ctrl_track(reflow_setpoint(), reflow_rate());
        return PROC_EV_NONE;
    }
}

static proc_event_t step_tune(temp_dc_t pv)
{
    switch (tune_step(pv)) {
    case TUNE_DONE:
        store_tune_results();
        proc_off();
        return PROC_EV_DONE;
    case TUNE_FAILED:
        proc_off();
        return PROC_EV_FAILED;
    default:
        ctrl_manual(tune_duty());
        return PROC_EV_NONE;
    }
}

proc_event_t proc_step(temp_dc_t pv)
{
    proc_event_t ev = PROC_EV_NONE;

    p.pv = pv;
    if (p.mode == PROC_REFLOW)
        ev = step_reflow(pv);
    else if (p.mode == PROC_TUNE)
        ev = step_tune(pv);
    p.duty = ctrl_update(pv);
    return ev;
}

proc_mode_t proc_mode(void)
{
    return p.mode;
}

duty_t proc_duty(void)
{
    return p.duty;
}

temp_dc_t proc_setpoint(void)
{
    return p.mode == PROC_OFF ? 0 : ctrl_setpoint();
}
