#include "sensor/adc.h"
#include "board/pins.h"

#define ADCCON1_ADCEN   0x01
#define ADCCON1_DIV2    (1u << 4)       /* ADC clock = Fsys / 2 */
#define ADCCON2_AQT5    (5u << 1)       /* acquisition time setting 5 */
#define ADCCON0_ADCF    0x80

static __idata uint32_t acc;
static __idata uint16_t count;
static volatile __idata uint32_t window_sum;
static volatile __bit window_ready;

void adc_init(void)
{
    AINDIDS = ADC_AINDIDS_BIT;          /* P0.7 analog only */
    ADCCON0 = ADC_CHANNEL;              /* ADCHS = AIN2, flags clear */
    ADCCON1 = ADCCON1_DIV2 | ADCCON1_ADCEN;
    ADCCON2 = (ADCCON2 & 0xF1) | ADCCON2_AQT5;
    ADCS = 1;                           /* first conversion */
}

bool adc_window_ready(void)
{
    return window_ready;
}

uint16_t adc_take_reading(void)
{
    uint32_t sum;

    EA = 0;
    sum = window_sum;
    window_ready = 0;
    EA = 1;
    /* mean of 12-bit samples (0..4095) * 16 */
    return (uint16_t)((sum << 4) / ADC_SAMPLES_PER_WINDOW);
}

/* Runs in the tick interrupt: no locals, no calls, no library arithmetic. */
void adc_scan(void)
{
    if (ADCF) {
        ADCF = 0;
        acc += ((uint16_t)ADCRH << 4) | (ADCRL & 0x0F);
        if (++count >= ADC_SAMPLES_PER_WINDOW) {
            window_sum = acc;
            window_ready = 1;
            acc = 0;
            count = 0;
        }
    }
    ADCS = 1;                           /* convert during the quiet slot */
}
