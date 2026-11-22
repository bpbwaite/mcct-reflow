#include "test.h"
#include "storage/settings.h"
#include "storage/flash.h"
#include "common/config.h"

void test_settings(void)
{
    static __xdata uint8_t i;

    /* blank flash: defaults */
    fake_flash_erase_all();
    settings.target_c = 0;
    settings_load();
    CHECK_EQ(settings.target_c, CFG_TARGET_DEFAULT_C);
    CHECK_EQ(settings.kp_x10, CFG_PID_KP_X10_DEFAULT);

    /* round trip */
    settings.target_c = 222;
    settings.offset_c = -3;
    settings.kp_x10 = 123;
    settings_save();
    settings_defaults();
    settings_load();
    CHECK_EQ(settings.target_c, 222);
    CHECK_EQ(settings.offset_c, -3);
    CHECK_EQ(settings.kp_x10, 123);

    /* many saves wrap around both pages; the newest always wins */
    for (i = 0; i < 40; i++) {
        settings.target_c = 100 + i;
        settings_save();
    }
    settings_defaults();
    settings_load();
    CHECK_EQ(settings.target_c, 139);

    /* a torn write (bad CRC) falls back to the previous record ... */
    settings.target_c = 250;
    settings_save();                                    /* 42nd save -> slot 9 */
    fake_flash_poke(SETTINGS_FLASH_BASE + 9 * 16 + 15, 0x00);
    settings_load();
    CHECK_EQ(settings.target_c, 139);
    /* ... and the next save still lands (the damaged slot is erased first) */
    settings.target_c = 260;
    settings_save();
    settings_defaults();
    settings_load();
    CHECK_EQ(settings.target_c, 260);

    /* stored values outside the current limits are brought back in range */
    settings.target_c = 1000;
    settings.offset_c = 100;
    settings_save();
    settings_load();
    CHECK_EQ(settings.target_c, CFG_TARGET_MAX_C);
    CHECK_EQ(settings.offset_c, CFG_OFFSET_LIMIT_C);

    /* deferred save: only CFG_SAVE_DELAY_MS after the last change */
    fake_now = 1000;
    settings.target_c = 180;
    settings_changed();
    fake_now += CFG_SAVE_DELAY_MS - 1;
    settings_service();
    settings.target_c = 0;
    settings_load();
    CHECK_EQ(settings.target_c, CFG_TARGET_MAX_C);      /* not written yet */
    settings.target_c = 180;
    settings_changed();
    fake_now += CFG_SAVE_DELAY_MS;
    settings_service();
    settings.target_c = 0;
    settings_load();
    CHECK_EQ(settings.target_c, 180);
}
