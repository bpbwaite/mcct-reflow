#include "app/ui.h"
#include "board/tick.h"
#include "display/display.h"
#include "display/render.h"
#include "control/heater.h"
#include "control/profile.h"
#include "control/pid.h"
#include "storage/settings.h"
#include "common/config.h"
#include "common/fixed.h"
#include <stddef.h>

typedef enum {
    SCR_SPLASH, SCR_STANDBY, SCR_HEAT, SCR_MENU, SCR_EDIT,
    SCR_RUN, SCR_IDLE, SCR_MESSAGE, SCR_FATAL
} screen_t;

/* Settings menu. Each item: its label, and the range of the value edited
 * when it is opened (reflow: profile index, tune: target in degC, P: tenths
 * of %/degC, I/d: seconds, OFS: degC, rSt: 0 = no / 1 = yes). */
enum { ITEM_REFLOW, ITEM_TUNE, ITEM_KP, ITEM_TI, ITEM_TD, ITEM_OFFSET, ITEM_RESET, ITEM_COUNT };

typedef struct {
    char    label[4];
    int16_t lo, hi;
} menu_item_t;

static const menu_item_t __code items[ITEM_COUNT] = {
    { "rFL", 0, PROFILE_MAX - 1 },
    { "tun", CFG_TARGET_MIN_C, CFG_TARGET_MAX_C },
    { "P",   1, PID_GAIN_MAX },
    { "I",   0, PID_GAIN_MAX },
    { "d",   0, PID_GAIN_MAX },
    { "OFS", -CFG_OFFSET_LIMIT_C, CFG_OFFSET_LIMIT_C },
    { "rSt", 0, 1 },
};

#define IDLE_STEPS      SECONDS_TO_STEPS(CFG_IDLE_TIMEOUT_S)
#define FLASH_PHASES    4           /* off, on, off, on = "flashes twice" */

static __xdata struct {
    screen_t    screen;
    ms_t        now;                /* time of the event being handled */
    ms_t        entered;            /* blink phase reference */
    ms_t        last_input;
    ms_t        adjusted;           /* last target change */
    uint8_t     previewing;         /* target just changed: show it, solid */
    uint8_t     item;
    int16_t     value;              /* value being edited */
    uint8_t     flashing;
    ms_t        flash_start;
    uint16_t    idle_steps;
    temp_dc_t   pv;
    const char *message;
} ui;

/* ------------------------------------------------------------ helpers */

static void go(screen_t s)
{
    ui.screen = s;
    ui.entered = ui.now;
    ui.last_input = ui.now;
    ui.previewing = 0;
}

static void show_message(const char *text)
{
    ui.message = text;
    go(SCR_MESSAGE);
}

static void open_menu(void)
{
    ui.item = 0;
    go(SCR_MENU);
}

static void leave_menu(void)
{
    go(proc_mode() == PROC_HOLD ? SCR_HEAT : SCR_STANDBY);
}

static void stop_to_standby(void)
{
    proc_off();
    go(SCR_STANDBY);
}

static void adjust_target(int8_t delta)
{
    settings.target_c = fx_clamp16(settings.target_c + delta, CFG_TARGET_MIN_C, CFG_TARGET_MAX_C);
    settings_changed();
    ui.previewing = 1;
    ui.adjusted = ui.now;
}

/* ------------------------------------------------------- edit screens */

/* Kp, Ti and Td are consecutive uint16_t fields of settings_t. */
_Static_assert(offsetof(settings_t, td_s) == offsetof(settings_t, kp_x10) + 2 * sizeof(uint16_t),
               "gain fields must be consecutive");

static __xdata uint16_t *gain_field(void)
{
    return &settings.kp_x10 + (ui.item - ITEM_KP);
}

static void edit_begin(void)
{
    switch (ui.item) {
    case ITEM_REFLOW: ui.value = settings.profile; break;
    case ITEM_TUNE:   ui.value = CFG_TUNE_DEFAULT_C; break;
    case ITEM_OFFSET: ui.value = settings.offset_c; break;
    case ITEM_RESET:  ui.value = 0; break;
    default:          ui.value = (int16_t)*gain_field(); break;
    }
    go(SCR_EDIT);
}

static void edit_step(int8_t delta)
{
    int16_t lo = items[ui.item].lo;
    int16_t hi = ui.item == ITEM_REFLOW ? profile_count - 1 : items[ui.item].hi;
    int16_t v = ui.value + delta;

    if (ui.item == ITEM_REFLOW || ui.item == ITEM_RESET)
        v = v > hi ? lo : (v < lo ? hi : v);                /* choices wrap */
    ui.value = fx_clamp16(v, lo, hi);
}

static void edit_commit(void)
{
    switch (ui.item) {
    case ITEM_REFLOW:
        settings.profile = (uint8_t)ui.value;
        settings_changed();
        proc_reflow((uint8_t)ui.value);
        go(SCR_RUN);
        return;
    case ITEM_TUNE:
        proc_tune(TEMP_DC(ui.value));
        go(SCR_RUN);
        return;
    case ITEM_OFFSET:
        settings.offset_c = (int8_t)ui.value;
        break;
    case ITEM_RESET:
        if (ui.value)
            settings_defaults();
        break;
    default:
        *gain_field() = (uint16_t)ui.value;
        break;
    }
    settings_changed();
    proc_apply_settings();
    if (proc_mode() == PROC_HOLD)
        proc_hold(TEMP_DC(settings.target_c));
    go(SCR_MENU);
}

/* ------------------------------------------------------------- events */

void ui_init(void)
{
    ui.now = clock_ms();
    ui.flashing = 0;
    ui.idle_steps = 0;
    go(SCR_SPLASH);
}

void ui_fatal(void)
{
    ui.now = clock_ms();
    go(SCR_FATAL);
}

void ui_button(btn_event_t ev)
{
    int8_t delta = (ev == BTN_UP) ? 1 : (ev == BTN_DOWN) ? -1 : 0;

    if (ev == BTN_NONE)
        return;
    ui.now = clock_ms();
    ui.idle_steps = 0;
    ui.last_input = ui.now;

    switch (ui.screen) {
    case SCR_STANDBY:
        if (delta)
            adjust_target(delta);
        else if (ev == BTN_ENTER) {
            proc_hold(TEMP_DC(settings.target_c));
            go(SCR_HEAT);
        } else
            open_menu();
        break;

    case SCR_HEAT:
        if (delta) {
            adjust_target(delta);
            proc_hold(TEMP_DC(settings.target_c));
        } else if (ev == BTN_ENTER)
            stop_to_standby();
        else
            open_menu();
        break;

    case SCR_MENU:
        if (delta > 0)
            ui.item = ui.item + 1 < ITEM_COUNT ? ui.item + 1 : 0;
        else if (delta < 0)
            ui.item = ui.item ? ui.item - 1 : ITEM_COUNT - 1;
        else if (ev == BTN_ENTER)
            edit_begin();
        else
            leave_menu();
        break;

    case SCR_EDIT:
        if (delta)
            edit_step(delta);
        else if (ev == BTN_ENTER)
            edit_commit();
        else
            go(SCR_MENU);
        break;

    case SCR_RUN:                           /* any button cancels */
    case SCR_MESSAGE:
    case SCR_IDLE:
        stop_to_standby();
        break;

    default:                                /* splash, fatal: ignore */
        break;
    }
}

void ui_process_event(proc_event_t ev)
{
    if (ui.screen != SCR_RUN || ev == PROC_EV_NONE)
        return;
    ui.now = clock_ms();
    if (ev == PROC_EV_WAYPOINT) {
        ui.flashing = 1;
        ui.flash_start = ui.now;
    } else {
        show_message(ev == PROC_EV_DONE ? "End" : "Err");
    }
}

void ui_sample(temp_dc_t pv)
{
    ui.pv = pv;
    if (ui.idle_steps < IDLE_STEPS)
        ui.idle_steps++;
    /* Automatic runs (reflow, tune) have their own time limits. */
    if (ui.idle_steps >= IDLE_STEPS && pv > TEMP_DC(CFG_IDLE_TIMEOUT_C)
        && ui.screen >= SCR_STANDBY && ui.screen <= SCR_EDIT) {
        ui.now = clock_ms();
        proc_off();
        go(SCR_IDLE);
    }
}

/* ---------------------------------------------------------- rendering */

static void render_idle_animation(uint8_t step)
{
    /* A three-segment snake running around the outside of the display. */
    static const uint8_t __code path_digit[10] = { 0, 1, 2, 2, 2, 2, 1, 0, 0, 0 };
    static const uint8_t __code path_seg[10] = {
        SEG_A, SEG_A, SEG_A, SEG_B, SEG_C, SEG_D, SEG_D, SEG_D, SEG_E, SEG_F
    };
    uint8_t i, k;

    render_blank();
    for (i = 0; i < 3; i++) {
        k = step + 10 - i;
        if (k >= 10)
            k -= 10;
        render_frame[path_digit[k]] |= path_seg[k];
    }
}

/* Draw the current screen; returns true if it blinks. */
static bool compose(void)
{
    uint8_t step = (uint8_t)(ui.now / CFG_ANIMATION_STEP_MS);

    switch (ui.screen) {
    case SCR_SPLASH:
        render_text("[=]");
        return false;
    case SCR_STANDBY:
        render_int(settings.target_c);
        return !ui.previewing;
    case SCR_HEAT:
        if (ui.previewing) {
            render_int(settings.target_c);
            return false;
        }
        render_int(temp_round_c(ui.pv));
        if (heater_is_on())
            render_dp(2);
        return false;
    case SCR_MENU:
        render_text(items[ui.item].label);
        return false;
    case SCR_EDIT:
        if (ui.item == ITEM_REFLOW)
            render_text(profiles[ui.value].label);
        else if (ui.item == ITEM_KP)
            render_tenths(ui.value);
        else if (ui.item == ITEM_RESET)
            render_text(ui.value ? "yES" : "no");
        else
            render_int(ui.value);
        return false;
    case SCR_RUN:
        render_int(temp_round_c(ui.pv));
        step &= 3;
        render_dp(step == 3 ? 1 : step);    /* DP bounces 0 1 2 1 */
        return false;
    case SCR_IDLE:
        render_idle_animation(step % 10u);
        return false;
    case SCR_MESSAGE:
        render_text(ui.message);
        return true;
    default:                                /* SCR_FATAL */
        render_text("uuu");
        return true;
    }
}

void ui_render(void)
{
    uint8_t phase;

    ui.now = clock_ms();
    if (ui.screen == SCR_SPLASH && MS_ELAPSED(ui.now, ui.entered) >= CFG_SPLASH_MS)
        go(SCR_STANDBY);
    if ((ui.screen == SCR_MENU || ui.screen == SCR_EDIT)
        && MS_ELAPSED(ui.now, ui.last_input) >= CFG_MENU_TIMEOUT_MS)
        leave_menu();

    if (ui.previewing && MS_ELAPSED(ui.now, ui.adjusted) >= CFG_PREVIEW_MS) {
        ui.previewing = 0;
        ui.entered = ui.now;                /* resume blinking with "on" */
    }

    if (compose() && MS_ELAPSED(ui.now, ui.entered) % CFG_BLINK_PERIOD_MS >= CFG_BLINK_ON_MS)
        render_blank();

    if (ui.flashing) {                      /* waypoint: off, on, off, on */
        phase = (uint8_t)(MS_ELAPSED(ui.now, ui.flash_start) / CFG_WAYPOINT_FLASH_MS);
        if (phase >= FLASH_PHASES)
            ui.flashing = 0;
        else if (!(phase & 1))
            render_blank();
    }
    display_write(render_frame);
}
