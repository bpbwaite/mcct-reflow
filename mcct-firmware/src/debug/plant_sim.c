#include "debug/plant_sim.h"
#include "common/config.h"

#if CFG_PLANT_SIM

/* The real plate as measured on 2026-11-22 (mcct-tuning/plate_model.py):
 * 184 J/K, heat reaches it ~11 s after the heater (element 20 J/K through
 * 2 W/K: tau 10 s), losses ~0.32 W/K averaged over 100..250 degC. */
#define HEATER_MW_PER_DUTY  35          /* 350 W / 10000 */
#define ELEMENT_J_PER_K     20
#define PLATE_J_PER_K       165
#define LINK_MW_PER_MK      2           /* 2 W/K */
#define LOSS_MW_PER_MK_X100 32          /* 0.32 W/K */
#define NOISE_MK            60          /* +/- sensor noise */

/* Temperatures in milli-kelvin above 0 degC, powers in mW. */
static __xdata struct {
    int32_t plate_mk, element_mk, ambient_mk;
    int32_t link_mw;            /* scratch for sim_step() */
    uint16_t lfsr;
} sim;

void sim_reset(temp_dc_t ambient)
{
    sim.ambient_mk = (int32_t)ambient * 100;
    sim.plate_mk = sim.ambient_mk;
    sim.element_mk = sim.ambient_mk;
    sim.lfsr = 0xACE1;
}

/* Pseudo-random sensor noise within +/- NOISE_MK (16-bit LCG). */
static int16_t noise(void)
{
    sim.lfsr = sim.lfsr * 25173u + 13849u;
    return (int16_t)(int8_t)(sim.lfsr >> 8) * NOISE_MK / 128;
}

temp_dc_t sim_step(duty_t duty)
{
    /* Euler step: mW * period = mJ, mJ / (J/K) = mK */
    sim.link_mw = (sim.element_mk - sim.plate_mk) * LINK_MW_PER_MK;
    sim.element_mk += ((int32_t)duty * HEATER_MW_PER_DUTY - sim.link_mw) / (CONTROL_STEPS_PER_S * ELEMENT_J_PER_K);
    sim.plate_mk += (sim.link_mw - (sim.plate_mk - sim.ambient_mk) * LOSS_MW_PER_MK_X100 / 100)
                    / (CONTROL_STEPS_PER_S * PLATE_J_PER_K);
    return (temp_dc_t)((sim.plate_mk + noise()) / 100);
}
#endif
