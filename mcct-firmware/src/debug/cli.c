#include "debug/cli.h"

#if CFG_CONSOLE
#include "debug/console.h"
#include "input/buttons.h"
#include "app/process.h"
#include "control/pid.h"
#include "control/heater.h"
#include "control/thermal_model.h"
#include "sensor/temperature.h"
#include "storage/settings.h"

static __xdata uint8_t telemetry_on;

static void put_field(int32_t v)
{
    console_put_int(v);
    console_putc(',');
}

void cli_banner(void)
{
    console_puts("\nMCCT hot plate  (h = help)\n");
}

void cli_report(const char *what, int16_t value)
{
    console_puts(what);
    console_put_int(value);
    console_putc('\n');
}

/* Settings as one CSV record, in their stored units (fields: see cli.h). */
static void status(void)
{
    put_field(settings.target_c);
    put_field(settings.offset_c);
    put_field(settings.profile + 1);
    put_field(settings.kp_x10);
    put_field(settings.ti_s);
    put_field(settings.td_s);
    put_field(settings.loss_x1000);
    put_field(settings.cap_x10);
    console_put_int(model_ambient());
    console_putc('\n');
}

cli_action_t cli_service(void)
{
    switch (console_getc()) {
    case 'u': buttons_inject(BTN_UP); break;
    case 'd': buttons_inject(BTN_DOWN); break;
    case 'e': buttons_inject(BTN_ENTER); break;
    case 'E': buttons_inject(BTN_SET); break;
    case 't':
        telemetry_on = !telemetry_on;
        if (telemetry_on)
            console_puts("ms,mv,pv,sp,duty,heater,mode,p,i,d\n");
        break;
    case 's': status(); break;
#if CFG_PLANT_SIM
    case 'S': return CLI_TOGGLE_SIM;
    case 'W':                   /* bench test: hang, so the watchdog must reset us */
        for (;;)
            ;
#endif
    case 'h':
    case '?':
        console_puts("u d e E: keys  t: telemetry  s: status"
#if CFG_PLANT_SIM
                     "  S: simulation"
#endif
                     "\n");
        break;
    default:
        break;
    }
    return CLI_NONE;
}

void cli_telemetry(const cli_sample_t *s)
{
    if (!telemetry_on)
        return;
    put_field(s->now);
    put_field(s->sim ? -1 : (int32_t)temp_fraction_to_mv(s->u_q16));
    put_field(s->pv);
    put_field(proc_setpoint());
    put_field(heater_duty());
    put_field(heater_is_on());
    put_field(proc_mode());
    put_field(pid_terms.p);
    put_field(pid_terms.integral_q8 >> 8);
    console_put_int(pid_terms.d);
    console_putc('\n');
}
#endif
