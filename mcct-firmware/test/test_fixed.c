#include "test.h"
#include "common/fixed.h"

void test_fixed(void)
{
    static const uint8_t __code check[] = "123456789";

    /* fx_mul_q16 must floor exactly like the generator's emulation */
    CHECK_EQ(fx_mul_q16(1000, 32768), 500);
    CHECK_EQ(fx_mul_q16(-1000, 32768), -500);
    CHECK_EQ(fx_mul_q16(-1, 1), -1);
    CHECK_EQ(fx_mul_q16(0x12345678L, 65535u), 305415235L);
    CHECK_EQ(fx_mul_q16(-0x12345678L, 40000u), -186413511L);

    CHECK_EQ(fx_clamp(5, 0, 3), 3);
    CHECK_EQ(fx_clamp(-5, 0, 3), 0);
    CHECK_EQ(fx_clamp16(2, 0, 3), 2);
    CHECK_EQ(fx_clamp16(-200, -100, 100), -100);

    CHECK_EQ(temp_round_c(1234), 123);
    CHECK_EQ(temp_round_c(1235), 124);
    CHECK_EQ(temp_round_c(4), 0);
    CHECK_EQ(temp_round_c(-1234), -123);
    CHECK_EQ(temp_round_c(-1235), -124);

    CHECK_EQ(crc8(0, (const uint8_t *)check, 9), 0xF4);    /* CRC-8/SMBUS check value */
}
