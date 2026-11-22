#include "common/fixed.h"

int32_t fx_mul_q16(int32_t a, uint16_t u)
{
    /* a = hi * 65536 + lo, with hi signed and lo unsigned, so
     * a * u / 65536 = hi * u + lo * u / 65536 and neither product overflows. */
    int16_t  hi = (int16_t)(a >> 16);
    uint16_t lo = (uint16_t)a;
    return (int32_t)hi * u + (int32_t)(((uint32_t)lo * u) >> 16);
}

int32_t fx_clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int16_t fx_clamp16(int16_t v, int16_t lo, int16_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int16_t temp_round_c(temp_dc_t t)
{
    /* unsigned division: avoids pulling in the signed-division routine */
    return (t >= 0) ? (int16_t)(((uint16_t)t + 5u) / 10u) : -(int16_t)(((uint16_t)-t + 5u) / 10u);
}

uint8_t crc8(uint8_t crc, const uint8_t *data, uint8_t n)
{
    uint8_t bit;
    while (n--) {
        crc ^= *data++;
        for (bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}
