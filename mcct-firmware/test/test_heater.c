#include "test.h"
#include "control/heater.h"
#include "common/config.h"

static __xdata uint16_t on_ms, shortest_on, run_len;
static __xdata uint8_t last;

static __xdata duty_t duty;
static __xdata uint8_t command;

/* Run `duration` ms at 1 kHz at `duty`, re-commanding it every 256 ms if
 * `command` is set. */
static void run(uint16_t duration)
{
    static __xdata uint16_t t;

    for (t = 0; t < duration; t++) {
        if (command && (t & 0xFF) == 0)
            heater_command(duty);
        fake_now++;
        heater_service();
        if (fake_heater_pin)
            on_ms++;
        if (fake_heater_pin == last) {
            run_len++;
        } else {
            if (last && run_len < shortest_on)
                shortest_on = run_len;
            run_len = 1;
            last = fake_heater_pin;
        }
    }
}

static void start_count(void)
{
    on_ms = 0;
    shortest_on = 65535;
}

void test_heater(void)
{
    fake_now = 0;
    heater_init();
    CHECK_EQ(fake_heater_pin, 0);

    /* 25 % over 10 s: the average matches and every pulse lasts a quantum */
    start_count();
    duty = DUTY_PERCENT(25); command = 1; run(10000);
    CHECK_NEAR(on_ms, 2500, HEATER_QUANTUM_MS);
    CHECK(shortest_on >= HEATER_QUANTUM_MS - 1);

    /* small duty: rare but full-length pulses, still the right average */
    start_count();
    duty = DUTY_PERCENT(3); command = 1; run(20000);
    CHECK_NEAR(on_ms, 600, HEATER_QUANTUM_MS);
    CHECK(shortest_on >= HEATER_QUANTUM_MS - 1);

    /* 100 % is continuously on */
    start_count();
    duty = DUTY_FULL; command = 1; run(2000);
    CHECK_NEAR(on_ms, 2000, HEATER_QUANTUM_MS);

    /* dead-man: no command for longer than CFG_HEATER_DEADMAN_MS -> off */
    start_count();
    duty = DUTY_FULL; command = 0; run(CFG_HEATER_DEADMAN_MS + 300);
    CHECK(on_ms <= CFG_HEATER_DEADMAN_MS + HEATER_QUANTUM_MS);
    CHECK_EQ(fake_heater_pin, 0);
    CHECK_EQ(heater_duty(), 0);

    /* duty 0 is off immediately, even mid-quantum */
    duty = DUTY_FULL; command = 1; run(500);
    heater_command(0);
    heater_service();
    CHECK_EQ(fake_heater_pin, 0);

    /* output disabled (simulator): the logic runs, the pin stays low */
    heater_enable_output(false);
    start_count();
    duty = DUTY_FULL; command = 1; run(500);
    CHECK_EQ(on_ms, 0);
    CHECK(heater_is_on());
    heater_enable_output(true);
    heater_off_now();
    CHECK_EQ(fake_heater_pin, 0);
}
