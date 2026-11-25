/* The user interface, driven through button events and the clock. */
#include "test.h"
#include "app/ui.h"
#include "app/process.h"
#include "storage/settings.h"
#include "control/profile.h"
#include "common/config.h"

static void at(uint16_t ms)
{
    fake_now = ms;
    ui_render();
}

static void press(btn_event_t ev)
{
    ui_button(ev);
    ui_render();
}

void test_ui(void)
{
    static __xdata uint16_t i;

    fake_flash_erase_all();
    settings_load();
    proc_off();
    proc_step(TEMP_DC(25));
    ui_sample(TEMP_DC(25));

    /* boot: "[=]", then the target flashes */
    fake_now = 0;
    ui_init();
    at(10);
    CHECK(frame_is_text("[=]"));
    at(CFG_SPLASH_MS + 10);
    CHECK(frame_is_int(CFG_TARGET_DEFAULT_C));
    at(CFG_SPLASH_MS + 10 + CFG_BLINK_ON_MS + 10);
    CHECK(frame_is_blank());

    /* UP/DOWN adjust the target, clamped to the range */
    press(BTN_UP);
    CHECK_EQ(settings.target_c, CFG_TARGET_DEFAULT_C + 1);

    /* ... and the target stays solid until a while after the last press,
     * then blinks again, starting "on" */
    for (i = 0; i < 40; i++) {              /* held: a repeat every 200 ms */
        at(fake_now + 200);
        CHECK(frame_is_int(settings.target_c));
        press(BTN_UP);
    }
    for (i = 0; i < CFG_PREVIEW_MS / 100; i++) {
        at(fake_now + 100);
        CHECK(frame_is_int(settings.target_c));
    }
    at(fake_now + CFG_BLINK_ON_MS + 50);
    CHECK(frame_is_blank());
    for (i = 0; i < 400; i++)
        ui_button(BTN_UP);
    CHECK_EQ(settings.target_c, CFG_TARGET_MAX_C);
    for (i = 0; i < 500; i++)
        ui_button(BTN_DOWN);
    CHECK_EQ(settings.target_c, CFG_TARGET_MIN_C);
    for (i = 0; i < 170; i++)
        ui_button(BTN_UP);
    CHECK_EQ(settings.target_c, 200);

    /* ENTER heats: the current temperature shows, steady */
    press(BTN_ENTER);
    CHECK_EQ(proc_mode(), PROC_HOLD);
    ui_sample(TEMP_DC(27));
    at(fake_now + 300);
    CHECK(frame_is_int(27));
    at(fake_now + 700);
    CHECK(!frame_is_blank());

    /* adjusting while heating previews the target, then returns */
    press(BTN_DOWN);
    CHECK(frame_is_int(199));
    at(fake_now + CFG_BLINK_ON_MS + 50);    /* solid, not blinking */
    CHECK(frame_is_int(199));
    at(fake_now + CFG_PREVIEW_MS + 100);
    CHECK(frame_is_int(27));

    /* SET: settings menu; UP/DOWN browse; SET goes back */
    press(BTN_SET);
    CHECK(frame_is_text("rFL"));
    press(BTN_UP);
    CHECK(frame_is_text("tun"));
    press(BTN_UP);
    CHECK(frame_is_text("P"));
    press(BTN_ENTER);                                       /* edit Kp */
    CHECK(frame_is_text(" 3.0"));    /* CFG_PID_KP_X10_DEFAULT, right-aligned */
    press(BTN_UP);
    press(BTN_ENTER);
    CHECK_EQ(settings.kp_x10, CFG_PID_KP_X10_DEFAULT + 1);
    CHECK(frame_is_text("P"));
    press(BTN_SET);
    CHECK_EQ(proc_mode(), PROC_HOLD);                       /* still heating */

    /* the menu times out back to the main screen */
    press(BTN_SET);
    at(fake_now + CFG_MENU_TIMEOUT_MS + 10);
    CHECK(!frame_is_text("rFL"));

    /* ENTER stops heating */
    press(BTN_ENTER);
    CHECK_EQ(proc_mode(), PROC_OFF);

    /* reflow: pick a profile by its label, start, any button cancels */
    press(BTN_SET);
    press(BTN_ENTER);
    CHECK(frame_is_text(profiles[settings.profile].label));
    press(BTN_UP);
    CHECK(frame_is_text(profiles[1].label));
    press(BTN_ENTER);
    CHECK_EQ(proc_mode(), PROC_REFLOW);
    CHECK_EQ(settings.profile, 1);
    press(BTN_DOWN);
    CHECK_EQ(proc_mode(), PROC_OFF);

    /* a finished run shows "End" until a button is pressed */
    press(BTN_SET);
    press(BTN_ENTER);
    press(BTN_ENTER);
    ui_process_event(PROC_EV_DONE);
    at(fake_now + 10);
    CHECK(frame_is_text("End"));
    press(BTN_ENTER);
    CHECK_EQ(proc_mode(), PROC_OFF);

    /* idle timeout: hot and untouched for CFG_IDLE_TIMEOUT_S -> heater off,
     * animation; a button returns to standby */
    press(BTN_ENTER);
    CHECK_EQ(proc_mode(), PROC_HOLD);
    for (i = 0; i < SECONDS_TO_STEPS(CFG_IDLE_TIMEOUT_S) - 10; i++)
        ui_sample(TEMP_DC(200));
    CHECK_EQ(proc_mode(), PROC_HOLD);
    for (i = 0; i < 20; i++)
        ui_sample(TEMP_DC(200));
    CHECK_EQ(proc_mode(), PROC_OFF);
    at(fake_now + 10);
    CHECK(!frame_is_blank());
    press(BTN_UP);
    at(fake_now + 10);
    CHECK(frame_is_int(settings.target_c));

    /* ... but not below CFG_IDLE_TIMEOUT_C */
    press(BTN_ENTER);
    for (i = 0; i < SECONDS_TO_STEPS(CFG_IDLE_TIMEOUT_S) + 100; i++)
        ui_sample(TEMP_DC(100));
    CHECK_EQ(proc_mode(), PROC_HOLD);
    press(BTN_ENTER);

    /* factory reset from the menu */
    settings.target_c = 123;
    press(BTN_SET);
    for (i = 0; i < 6; i++)
        press(BTN_UP);
    CHECK(frame_is_text("rSt"));
    press(BTN_ENTER);
    CHECK(frame_is_text("no"));
    press(BTN_UP);
    CHECK(frame_is_text("yES"));
    press(BTN_ENTER);
    CHECK_EQ(settings.target_c, CFG_TARGET_DEFAULT_C);

    /* fatal: "uuu" flashing, buttons ignored */
    ui_fatal();
    at(fake_now + 10);
    CHECK(frame_is_text("uuu"));
    press(BTN_ENTER);
    at(fake_now + 10);
    CHECK(frame_is_text("uuu"));
}
