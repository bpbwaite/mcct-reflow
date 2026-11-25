/*
 * MS51FB9AE (TSSOP-20) pin map. Hardware-only: include this from driver .c
 * files, never from logic modules (they must stay testable in the simulator).
 *
 *  pkg  port   use
 *   1   P0.5   segment D cathode
 *   2   P0.6   segment E cathode
 *   3   P0.7   AIN2  temperature voltage (after the amplifier/filter)
 *   4   P2.0   nRESET (Arduino bridge D13)
 *   5   P3.0   decimal-point cathode
 *   6   P1.7   segment C cathode
 *   7   VSS
 *   8   P1.6   ICPDA / UART1 TXD (console)
 *   9   VDD    5 V
 *  10   P1.5   digit 1 common anode (via resistor)
 *  11   P1.4   ENTER button to GND via 2k (pin is dedicated)
 *  12   P1.3   SET button to GND via 2k (pin is dedicated)
 *  13   P1.2   digit 2 common anode
 *  14   P1.1   segment F cathode + UP button
 *  15   P1.0   segment A cathode
 *  16   P0.0   digit 3 common anode
 *  17   P0.1   segment B cathode + DOWN button
 *  18   P0.2   ICPCK / UART1 RXD (console)
 *  19   P0.3   heater optocoupler: high = triac conducts
 *  20   P0.4   segment G cathode
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "numicro_8051.h"

#define PIN_SEG_A       P10
#define PIN_SEG_B       P01     /* shared with DOWN */
#define PIN_SEG_C       P17
#define PIN_SEG_D       P05
#define PIN_SEG_E       P06
#define PIN_SEG_F       P11     /* shared with UP */
#define PIN_SEG_G       P04
#define PIN_SEG_DP      P30

#define PIN_DIGIT1      P15
#define PIN_DIGIT2      P12
#define PIN_DIGIT3      P00

#define PIN_BTN_ENTER   P14
#define PIN_BTN_SET     P13
#define PIN_BTN_UP      PIN_SEG_F
#define PIN_BTN_DOWN    PIN_SEG_B

#define PIN_HEATER      P03

#define ADC_CHANNEL     2       /* AIN2 = P0.7 */
#define ADC_AINDIDS_BIT 0x04    /* digital input buffer off on AIN2 */

/* Timed access to TA-protected SFRs, with interrupts held off.
 * The protected write must be the very next instruction after TA = 0x55,
 * so the value is computed first and the bit forms compile to one ORL/ANL.
 * The SFR page must be 0; this firmware never selects page 1. */
#define TA_UNLOCK()             do { TA = 0xAA; TA = 0x55; } while (0)
#define TA_WRITE(sfr, value)    do { uint8_t v_ = (value); __bit ea_ = EA; EA = 0; \
                                     TA_UNLOCK(); (sfr) = v_; EA = ea_; } while (0)
#define TA_SET_BITS(sfr, mask)  do { __bit ea_ = EA; EA = 0; TA_UNLOCK(); (sfr) |= (mask); EA = ea_; } while (0)
#define TA_CLR_BITS(sfr, mask)  do { __bit ea_ = EA; EA = 0; TA_UNLOCK(); (sfr) &= (uint8_t)~(mask); EA = ea_; } while (0)

/* IAP (flash / CONFIG access) */
#define IAP_READ_CONFIG         0xC0
#define IAP_ERASE_APROM_PAGE    0x22
#define IAP_PROGRAM_APROM_BYTE  0x21
#define CHPCON_IAPEN            0x01
#define IAPUEN_APUEN            0x01
#define IAPTRG_IAPGO            0x01

#endif
