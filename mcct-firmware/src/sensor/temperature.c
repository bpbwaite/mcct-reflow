#include "sensor/temperature.h"
#include "common/config.h"
#include "common/fixed.h"
#include "calibration_gen.h"    /* generated: CAL_DEGREE, CAL_SHIFT, CAL_COEFFICIENTS */

static const int32_t __code coefficients[CAL_DEGREE + 1] = CAL_COEFFICIENTS;

temp_dc_t temp_from_fraction(uint16_t u_q16)
{
    /* Horner: c_n * u^n + ... + c_0, coefficients scaled by 2^CAL_SHIFT. The
     * generator picks CAL_SHIFT so that no partial sum can overflow and
     * checks this exact integer arithmetic against the fit. */
    int32_t acc = coefficients[CAL_DEGREE];
    int8_t k;

    for (k = CAL_DEGREE - 1; k >= 0; k--)
        acc = fx_mul_q16(acc, u_q16) + coefficients[k];
    acc = (acc + ((int32_t)1 << (CAL_SHIFT - 1))) >> CAL_SHIFT;
    return (temp_dc_t)fx_clamp(acc, -32000, 32000);
}

uint16_t temp_fraction_to_mv(uint16_t u_q16)
{
    return (uint16_t)(((uint32_t)u_q16 * CFG_VDD_MV) >> 16);
}
