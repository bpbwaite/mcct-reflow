#include "control/reflow.h"
#include "common/config.h"

enum { PHASE_RAMP, PHASE_SETTLE, PHASE_HOLD, PHASE_DONE };

#define TOLERANCE_DC    (CFG_REFLOW_TOLERANCE_C * 10)
#define SETTLE_STEPS    SECONDS_TO_STEPS(CFG_REFLOW_SETTLE_TIMEOUT_S)

static __xdata struct {
    uint8_t    wp, end;
    uint8_t    phase;
    uint16_t   steps;           /* control periods spent in this phase */
    temp_dc_t  sp;
    rate_cps_t rate;
    /* the current waypoint, unpacked */
    temp_dc_t  from;            /* where its ramp starts */
    temp_dc_t  target;
    int16_t    span;            /* target - from */
    uint16_t   ramp_steps;
    uint16_t   hold_steps;
    rate_cps_t ramp_rate;
} r;

static void next_phase(uint8_t phase)
{
    r.phase = phase;
    r.steps = 0;
}

static void load_waypoint(temp_dc_t from)
{
    const waypoint_t __code *w = &profile_waypoints[r.wp];

    r.from = from;
    r.target = w->temp;
    r.span = w->temp - from;
    r.ramp_steps = SECONDS_TO_STEPS(w->ramp_s);
    r.hold_steps = SECONDS_TO_STEPS(w->hold_s);
    r.ramp_rate = w->ramp_s ? (rate_cps_t)((int32_t)r.span * 10 / w->ramp_s) : 0;
    next_phase(PHASE_RAMP);
}

void reflow_start(uint8_t profile, temp_dc_t pv)
{
    if (profile >= profile_count)
        profile = 0;
    r.wp = profiles[profile].first;
    r.end = r.wp + profiles[profile].count;
    r.sp = pv;
    r.rate = 0;
    load_waypoint(pv);
}

reflow_event_t reflow_step(temp_dc_t pv)
{
    if (r.phase == PHASE_DONE)
        return REFLOW_EV_NONE;
    r.steps++;

    switch (r.phase) {
    case PHASE_RAMP:
        if (r.steps < r.ramp_steps) {
            r.sp = r.from + (temp_dc_t)((int32_t)r.span * r.steps / r.ramp_steps);
            r.rate = r.ramp_rate;
            return REFLOW_EV_NONE;
        }
        next_phase(PHASE_SETTLE);
        /* fall through: the ramp is over, start checking right away */
    case PHASE_SETTLE:
        r.sp = r.target;
        r.rate = 0;
        if (r.span >= 0 ? pv >= r.target - TOLERANCE_DC : pv <= r.target + TOLERANCE_DC) {
            next_phase(PHASE_HOLD);
            return REFLOW_EV_WAYPOINT;
        }
        /* Cooling is passive and takes as long as it takes; failing to
         * heat means the profile is too steep or something is wrong. */
        if (r.span >= 0 && r.steps > SETTLE_STEPS) {
            next_phase(PHASE_DONE);
            return REFLOW_EV_FAILED;
        }
        return REFLOW_EV_NONE;
    default: /* PHASE_HOLD */
        if (r.steps < r.hold_steps)
            return REFLOW_EV_NONE;
        if (++r.wp >= r.end) {
            next_phase(PHASE_DONE);
            return REFLOW_EV_DONE;
        }
        load_waypoint(r.target);
        return REFLOW_EV_NONE;
    }
}

temp_dc_t reflow_setpoint(void)
{
    return r.sp;
}

rate_cps_t reflow_rate(void)
{
    return r.rate;
}
