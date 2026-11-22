#include "control/heater.h"
#include "board/board.h"
#include "board/tick.h"
#include "common/config.h"

static __xdata struct {
    duty_t   duty;
    uint16_t sigma;             /* sigma-delta accumulator, 0 .. DUTY_FULL */
    ms_t     quantum_start;
    ms_t     last_command;
    uint8_t  on;
    uint8_t  output_enabled;
} h;

void heater_init(void)
{
    ms_t now = clock_ms();

    h.duty = 0;
    h.sigma = 0;
    h.on = 0;
    h.quantum_start = now;
    h.last_command = now;
    h.output_enabled = 1;
    board_heater_write(false);
}

void heater_command(duty_t duty)
{
    h.duty = duty > DUTY_FULL ? DUTY_FULL : duty;
    h.last_command = clock_ms();
}

void heater_off_now(void)
{
    h.duty = 0;
    h.sigma = 0;
    h.on = 0;
    board_heater_write(false);
}

void heater_enable_output(bool enable)
{
    h.output_enabled = enable;
}

void heater_service(void)
{
    ms_t now = clock_ms();

    if (MS_ELAPSED(now, h.last_command) > CFG_HEATER_DEADMAN_MS)
        h.duty = 0;

    if (MS_ELAPSED(now, h.quantum_start) >= HEATER_QUANTUM_MS) {
        h.quantum_start += HEATER_QUANTUM_MS;
        if (MS_ELAPSED(now, h.quantum_start) >= HEATER_QUANTUM_MS)
            h.quantum_start = now;              /* fell behind (flash write): resync */
        h.sigma += h.duty;
        if (h.sigma >= DUTY_FULL) {
            h.sigma -= DUTY_FULL;
            h.on = 1;
        } else {
            h.on = 0;
        }
    }
    if (h.duty == 0) {                          /* off means off, right now */
        h.on = 0;
        h.sigma = 0;
    }
    board_heater_write(h.on && h.output_enabled);
}

bool heater_is_on(void)
{
    return h.on;
}

duty_t heater_duty(void)
{
    return h.duty;
}
