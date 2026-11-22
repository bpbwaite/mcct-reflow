#include "test.h"
#include "control/pid.h"
#include "control/controller.h"
#include "control/thermal_model.h"
#include "common/config.h"

void test_pid(void)
{
    static __xdata uint16_t i;
    static __xdata duty_t u, prev;

    /* P: Kp 29.0 %/degC * 0.1 degC = 2.90 % (D off for this one) */
    pid_set_gains(CFG_PID_KP_X10_DEFAULT, CFG_PID_TI_S_DEFAULT, 0);
    pid_reset();
    CHECK_EQ(pid_update(2001, 2000, 3000), 3000 + 290 + 1);     /* + first integral step */

    pid_set_gains(CFG_PID_KP_X10_DEFAULT, CFG_PID_TI_S_DEFAULT, CFG_PID_TD_S_DEFAULT);

    /* on target and steady: output = feed-forward */
    pid_reset();
    CHECK_EQ(pid_update(2000, 2000, 3000), 3000);

    /* far below target: saturates, and the integral does not wind up */
    pid_reset();
    for (i = 0; i < 400; i++)
        CHECK_EQ(pid_update(2000, 250, 0), DUTY_FULL);
    CHECK_EQ(pid_terms.integral_q8, 0);

    /* a small persistent error is integrated away: output keeps rising */
    pid_reset();
    for (i = 0; i < 20; i++)                    /* let the derivative settle */
        prev = pid_update(2001, 2000, 3000);
    for (i = 0; i < 200; i++)
        u = pid_update(2001, 2000, 3000);
    CHECK(u > prev + 200);

    /* D brakes a fast approach (plate rising 1 degC/s towards a fixed
     * setpoint) ... */
    pid_reset();
    for (i = 0; i < 20; i++)
        u = pid_update(2000, 1950 + (int16_t)(i * 5 / 2), 5000);
    CHECK(pid_terms.d < 0);

    /* ... and adds nothing while the plate tracks a ramp exactly */
    pid_reset();
    for (i = 0; i < 20; i++)
        u = pid_update(2000 + (int16_t)(i * 5 / 2), 2000 + (int16_t)(i * 5 / 2), 5000);
    CHECK_EQ(pid_terms.d, 0);
    CHECK_EQ(u, 5000);

    /* Ti = 0 disables the integral; output never leaves 0..100 % */
    pid_set_gains(290, 0, 0);
    pid_reset();
    for (i = 0; i < 50; i++)
        u = pid_update(1990, 2000, 0);
    CHECK_EQ(u, 0);
    CHECK_EQ(pid_terms.integral_q8, 0);
}

void test_controller(void)
{
    static __xdata uint16_t i;
    static __xdata temp_dc_t sp, prev;
    static __xdata int16_t biggest_step;

    model_set_params(CFG_MODEL_LOSS_X1000_DEFAULT, CFG_MODEL_CAP_X10_DEFAULT);
    model_set_ambient(TEMP_DC(25));
    pid_set_gains(CFG_PID_KP_X10_DEFAULT, CFG_PID_TI_S_DEFAULT, CFG_PID_TD_S_DEFAULT);

    ctrl_off();
    CHECK_EQ(ctrl_update(TEMP_DC(25)), 0);

    /* HOLD ramps the setpoint from the plate temperature at the modelled
     * rate (~1 degC/s at room temperature with the defaults), never past
     * the target */
    ctrl_hold(TEMP_DC(200));
    prev = ctrl_setpoint();
    CHECK_EQ(prev, TEMP_DC(25));
    biggest_step = 0;
    for (i = 0; i < 2000; i++) {
        ctrl_update(TEMP_DC(25));
        sp = ctrl_setpoint();
        CHECK(sp >= prev && sp <= TEMP_DC(200));
        if (sp - prev > biggest_step)
            biggest_step = sp - prev;
        prev = sp;
    }
    CHECK_EQ(sp, TEMP_DC(200));
    CHECK(biggest_step >= 2 && biggest_step <= 4);      /* 0.2 .. 0.4 degC per 250 ms */

    /* lowering the target ramps down at the natural cooling rate */
    ctrl_hold(TEMP_DC(100));
    ctrl_update(TEMP_DC(200));
    CHECK(ctrl_setpoint() < TEMP_DC(200));
    CHECK(ctrl_setpoint() > TEMP_DC(199));
    for (i = 0; i < 4000; i++)
        ctrl_update(TEMP_DC(200));
    CHECK_EQ(ctrl_setpoint(), TEMP_DC(100));

    /* MANUAL passes the duty straight through */
    ctrl_manual(1234);
    CHECK_EQ(ctrl_update(TEMP_DC(100)), 1234);
}
