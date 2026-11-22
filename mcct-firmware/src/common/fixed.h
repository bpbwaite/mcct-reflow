/* Small fixed-point helpers (pure, no hardware). */
#ifndef COMMON_FIXED_H
#define COMMON_FIXED_H

#include "common/types.h"

/* floor(a * u / 65536) without a 48-bit intermediate: u is a Q16 fraction. */
int32_t fx_mul_q16(int32_t a, uint16_t u);

int32_t fx_clamp(int32_t v, int32_t lo, int32_t hi);
int16_t fx_clamp16(int16_t v, int16_t lo, int16_t hi);   /* cheaper: use when ranges fit */

/* Round deci-degrees to the nearest whole degree (halves away from zero). */
int16_t temp_round_c(temp_dc_t t);

/* CRC-8, polynomial 0x07, over n bytes starting from `crc`. */
uint8_t crc8(uint8_t crc, const uint8_t *data, uint8_t n);

#endif
