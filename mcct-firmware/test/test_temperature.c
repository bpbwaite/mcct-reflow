#include "test.h"
#include "sensor/temperature.h"
#include "calibration_gen.h"

typedef struct { uint16_t u; int16_t t; } vector_t;
static const vector_t __code vectors[] = CAL_TEST_VECTORS;

void test_temperature(void)
{
    static __xdata uint8_t i;

    /* The 8051 evaluation must match the generator's integer emulation bit
     * for bit (that is what its fit-quality report describes). */
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++)
        CHECK_EQ(temp_from_fraction(vectors[i].u), vectors[i].t);

    CHECK_EQ(temp_fraction_to_mv(32768u), 2500);
    CHECK_EQ(temp_fraction_to_mv(0), 0);
}
