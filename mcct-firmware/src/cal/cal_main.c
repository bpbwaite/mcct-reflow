/*
 * Sensor calibration firmware (.\build.ps1 -Cal). A separate program with
 * its own main(): it links only the drivers it needs and no temperature
 * logic. It holds the plate at a sensor VOLTAGE (cal_hold.c), so each
 * plateau pairs a steady reading with a steady reference probe.
 *
 * Display: the reading, mV below 1 V ("357"), else volts ("2.56"); after
 * UP/DOWN the target for 2 s. The rightmost decimal point means "holding".
 *
 *   UP / DOWN     target +/- CFG_CAL_STEP_MV (also while holding)
 *   ENTER         start / stop the hold
 *
 * Heater off on: raw ceiling, time limit, sensor not following, host lost
 * (host-started holds), sensor at the top rail, watchdog, missing samples.
 * Over-temperature protection stays external: the scale is being measured.
 *
 * Console (38400), one line per 250 ms: t_ms,q16,mv_x10,set_mv,duty,state
 * (duty 0.01 %, state = hold_state_t; t_ms counts windows, no wrap).
 * Keys: + - target, g start (then k at least every CFG_CAL_HOST_S), x stop.
 */
#include "common/config.h"
#include "board/board.h"
#include "board/tick.h"             /* tick_isr() prototype: must be visible here */
#include "display/display.h"
#include "display/render.h"
#include "input/buttons.h"
#include "sensor/adc.h"
#include "control/heater.h"
#include "debug/console.h"
#include "cal/cal_hold.h"

#define SAMPLE_TIMEOUT_MS   (4 * CFG_CONTROL_PERIOD_MS)
#define SENSOR_MAX_Q16      ((uint16_t)(65536UL * CFG_SENSOR_MAX_PERMILLE / 1000))
#define DISPLAY_WINDOWS     2u      /* average 2 windows: a new value every 500 ms */
#define SHOW_SET_WINDOWS    8u      /* 2 s */

static __xdata struct {
    uint32_t windows;           /* ADC windows since boot (log time base) */
    uint32_t disp_sum;          /* Q16 sum for the display average */
    uint16_t mv;                /* last displayed value, mV */
    uint16_t mv_x10;            /* last window, 0.1 mV */
    ms_t     last_sample;
    ms_t     last_poll;
    uint8_t  disp_count;
    uint8_t  show_set;          /* windows left showing the target */
    uint8_t  reported;          /* last hold_state announced */
} cal;

static void say(const char *what)
{
    console_puts("# ");
    console_puts(what);
    console_putc('\n');
}

static void show(void)
{
    uint16_t cv;
    uint16_t mv = cal.show_set ? hold_set_mv : cal.mv;

    if (mv < 1000) {
        render_int((int16_t)mv);
    } else {
        cv = (mv + 5) / 10;                 /* centivolts: "2.56" */
        if (cv > 999)
            cv = 999;
        render_frame[0] = font_glyph((char)('0' + cv / 100)) | SEG_DP;
        render_frame[1] = font_glyph((char)('0' + (cv / 10) % 10));
        render_frame[2] = font_glyph((char)('0' + cv % 10));
    }
    if (hold_state == HOLD_RUNNING)
        render_dp(2);
    display_write(render_frame);
}

static const char *state_text(void)
{
    switch (hold_state) {
    case HOLD_RUNNING:   return "holding";
    case HOLD_STOPPED:   return "heater off: operator";
    case HOLD_TIMEOUT:   return "heater off: time limit";
    case HOLD_CEILING:   return "heater off: mV ceiling";
    case HOLD_NO_RISE:   return "heater off: sensor does not follow";
    case HOLD_HOST_LOST: return "heater off: host lost";
    default:             return "idle";
    }
}

/* Heater off at once when not holding; announce state changes. */
static void sync(void)
{
    if (hold_state != HOLD_RUNNING)
        heater_off_now();
    if ((uint8_t)hold_state == cal.reported)
        return;
    cal.reported = (uint8_t)hold_state;
    say(state_text());
    show();
}

/* Heater off, "uuu" until the power is cycled. */
static void fatal(const char *why)
{
    heater_off_now();
    say(why);
    render_text("uuu");
    display_write(render_frame);
    for (;;) {
        board_watchdog_feed();
        heater_off_now();
    }
}

static void window(void)
{
    uint16_t q16 = adc_take_reading();
    duty_t duty;

    cal.windows++;
    if (q16 > SENSOR_MAX_Q16)
        fatal("FATAL sensor at top rail");

    cal.mv_x10 = (uint16_t)(((uint32_t)q16 * (CFG_VDD_MV * 10UL)) >> 16);
    duty = hold_update(cal.mv_x10);
    heater_command(duty);                   /* also refreshes the dead-man */
    sync();

    console_put_int((int32_t)(cal.windows * CFG_CONTROL_PERIOD_MS));
    console_putc(',');
    console_put_int(q16);
    console_putc(',');
    console_put_int(cal.mv_x10);
    console_putc(',');
    console_put_int(hold_set_mv);
    console_putc(',');
    console_put_int(duty);
    console_putc(',');
    console_put_int(hold_state);
    console_putc('\n');

    if (cal.show_set)
        cal.show_set--;
    cal.disp_sum += q16;
    if (++cal.disp_count >= DISPLAY_WINDOWS) {
        cal.mv = (uint16_t)(((cal.disp_sum / DISPLAY_WINDOWS) * CFG_VDD_MV) >> 16);
        cal.disp_sum = 0;
        cal.disp_count = 0;
        show();
    }
}

static void adjust(int8_t steps)
{
    hold_adjust(steps);
    cal.show_set = SHOW_SET_WINDOWS;
    show();
}

static void start(uint8_t host)
{
    if (hold_state != HOLD_RUNNING)
        hold_start(host, cal.mv_x10);
    sync();
}

static void stop(void)
{
    hold_stop();
    sync();
}

static void button(void)
{
    btn_event_t ev = buttons_poll(display_buttons_raw());

    if (ev == BTN_UP)
        adjust(1);
    else if (ev == BTN_DOWN)
        adjust(-1);
    else if (ev != BTN_NONE) {              /* ENTER, short or long */
        if (hold_state == HOLD_RUNNING)
            stop();
        else if (ev == BTN_ENTER)
            start(0);
    }
}

/* Host keys start or stop, never toggle: a late or repeated stop key cannot
 * turn the heater on. */
static void host_key(int16_t key)
{
    if (key == 'k')
        hold_keepalive();
    else if (key == 'g')
        start(1);
    else if (key == 'x')
        stop();
    else if (key == '+')
        adjust(1);
    else if (key == '-')
        adjust(-1);
}

void main(void)
{
    reset_cause_t cause = board_init();     /* heater pin low first */
    ms_t now;
    int16_t key;

    board_watchdog_init();
    display_init();
    adc_init();
    buttons_init();
    console_init();
    tick_init();
    heater_init();
    hold_init();

    render_text("[=]");
    display_write(render_frame);
    say("MCCT calibration: UP/DOWN target mV, ENTER start/stop");
    console_puts("t_ms,q16,mv_x10,set_mv,duty,state\n");

    if (cause == RESET_WATCHDOG)
        fatal("FATAL watchdog reset");
#if CFG_REQUIRE_HW_WATCHDOG
    if (!board_watchdog_from_config())
        fatal("FATAL watchdog not enabled in CONFIG");
#endif

    now = clock_ms();
    cal.last_sample = now;
    cal.last_poll = now;

    for (;;) {
        board_watchdog_feed();
        now = clock_ms();

        if (adc_window_ready()) {
            cal.last_sample = now;
            window();
        } else if (MS_ELAPSED(now, cal.last_sample) > SAMPLE_TIMEOUT_MS) {
            fatal("FATAL no ADC samples");
        }

        if (MS_ELAPSED(now, cal.last_poll) >= CFG_BUTTON_POLL_MS) {
            cal.last_poll = now;
            button();
        }

        key = console_getc();
        if (key >= 0)
            host_key(key);

        heater_service();
    }
}
