/*
 * MCCT firmware - MS51FB9AE (TSSOP-20, 16 KB APROM, 256 B IRAM, 1 KB XRAM)
 *
 * ADC example: samples package pin 3 (P0.7 / ADC_CH2) every 500 ms and prints
 * the 12-bit result on the UART1 debug console (see console.h), which the
 * Arduino bridge passes through to USB: run .\build.ps1 -Monitor.
 *
 *   pin 3  P0.7  ADC_CH2   <- analog input (0 .. VDD)
 *   pin 8  P1.6  UART1_TXD -> console out (shared with ICPDA)
 *   pin 18 P0.2  UART1_RXD <- console in  (shared with ICPCK)
 *
 * Console keys: 'p' pauses/resumes sampling, 'r' prints one reading now,
 * anything else is echoed back.
 */
#include <stdio.h>
#include "numicro_8051.h"
#include "console.h"

#define FSYS_HZ     24000000UL
#define VDD_MV      5000U       /* ADC reference is VDD; set to the real supply */

static uint16_t adc_read(void)
{
    uint16_t raw;

    clr_ADCCON0_ADCF;
    set_ADCCON0_ADCS;                   /* start conversion */
    while (!(ADCCON0 & SET_BIT7))       /* wait for ADCF */
        ;
    raw  = (uint16_t)ADCRH << 4;        /* 12-bit result: ADCRH[7:0] ADCRL[3:0] */
    raw |= ADCRL & 0x0F;
    return raw;
}

static void print_reading(void)
{
    uint16_t raw = adc_read();
    uint16_t mv  = (uint16_t)(((uint32_t)raw * VDD_MV) / 4095U);
    printf_tiny("ADC=%u  ~%u mV\n", raw, mv);
}

void main(void)
{
    uint8_t paused = 0;
    uint8_t ticks  = 0;
    int16_t c;

    MODIFY_HIRC(HIRC_24);               /* run from the trimmed 24 MHz HIRC */
    P12_PUSHPULL_MODE;                  /* LED */
    console_init(FSYS_HZ);

    ENABLE_ADC_AIN2;                    /* select ADC_CH2 (P0.7), P0.7 input-only, enable ADC */
    ADC_ConvertTime(ADC_ADCDIV2, ADC_ADCAQT5);

    printf_tiny("\nMCCT ADC test: pin 3 (P0.7 / ADC_CH2)\n");
    printf_tiny("keys: p = pause/resume, r = read now\n");

    while (1) {
        Timer0_Delay(FSYS_HZ, 10, 1000);        /* 10 x 1000 us = 10 ms tick (3rd arg is in us) */

        c = console_getc();
        if (c == 'p') {
            paused = !paused;
            printf_tiny(paused ? "paused\n" : "running\n");
        } else if (c == 'r') {
            print_reading();
        } else if (c >= 0) {
            printf_tiny("echo: %c\n", (char)c);
        }

        if (!paused && ++ticks >= 50) {         /* every 500 ms */
            ticks = 0;
            print_reading();
        }
    }
}
