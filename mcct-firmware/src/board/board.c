#include "board/board.h"
#include "board/pins.h"

/* WDCON bits */
#define WDCON_WDCLR     0x40
#define WDCON_WDTRF     0x08
#define WDCON_WDPS_MASK 0x07
#define WDCON_WDPS_410MS 0x05   /* 64 x 64 / 10 kHz LIRC = 409.6 ms */
#define PCON_POF        0x10
#define AUXR1_RESET_FLAGS 0xC0  /* SWRF | RSTPINF */
#define IAP_READ_UID    0x04
#define UID_HIRC24_TRIM 0x38    /* factory RCTRIM0/1 values for 24 MHz */

/*
 * Pin modes (MxM1, PxM2): quasi = 0,0  push-pull = 0,1  input = 1,0.
 * - Heater and digit anodes: push-pull, start low (off).
 * - Segment cathodes: quasi-bidirectional, latched high (off). Quasi mode
 *   sinks LED current strongly but only pulls up weakly, so the UP/DOWN/SET
 *   buttons sharing segment F/B/A can pull those lines low without fighting
 *   a push-pull driver, and the scan reads them back as inputs.
 * - ENTER: quasi (weak pull-up). P1.3 is not connected and stays quasi high.
 * - ADC pin: input only, digital buffer disabled in adc_init().
 * - UART pins: quasi, latched high.
 */
static void pins_init(void)
{
    PIN_HEATER = 0;             /* latch low before the pin is ever driven */
    P0   = 0xF6;                /* anode P0.0 and heater P0.3 low, rest high */
    P0M1 = 0x80;                /* P0.7 input */
    P0M2 = 0x09;                /* P0.0, P0.3 push-pull */
    P1   = 0xDB;                /* anodes P1.2, P1.5 low, rest high */
    P1M1 = 0x00;
    P1M2 = 0x24;                /* P1.2, P1.5 push-pull */
    P3   |= 0x01;               /* DP cathode high */
    P3M1 &= (uint8_t)~0x01;
    P3M2 &= (uint8_t)~0x01;     /* P3.0 quasi */
}

static reset_cause_t take_reset_cause(void)
{
    reset_cause_t cause;

    if (PCON & PCON_POF)
        cause = RESET_POWER_ON;
    else if (WDCON & WDCON_WDTRF)
        cause = RESET_WATCHDOG;
    else
        cause = RESET_OTHER;

    PCON &= (uint8_t)~PCON_POF;
    TA_CLR_BITS(WDCON, WDCON_WDTRF);
    AUXR1 &= (uint8_t)~AUXR1_RESET_FLAGS;
    return cause;
}

/* One IAP read (UID / CONFIG area). */
static uint8_t iap_read(uint8_t command, uint8_t addr)
{
    uint8_t value;

    TA_SET_BITS(CHPCON, CHPCON_IAPEN);
    IAPCN = command;
    IAPAH = 0;
    IAPAL = addr;
    TA_SET_BITS(IAPTRG, IAPTRG_IAPGO);
    value = IAPFD;
    TA_CLR_BITS(CHPCON, CHPCON_IAPEN);
    return value;
}

/* Switch the internal oscillator to 24 MHz with its factory trim (what
 * the BSP's MODIFY_HIRC(HIRC_24) does, without the other frequencies). */
static void clock_24mhz(void)
{
    uint8_t trim0 = iap_read(IAP_READ_UID, UID_HIRC24_TRIM);
    uint8_t trim1 = iap_read(IAP_READ_UID, UID_HIRC24_TRIM + 1);

    TA_WRITE(RCTRIM0, trim0);
    TA_WRITE(RCTRIM1, trim1);
}

reset_cause_t board_init(void)
{
    pins_init();
    clock_24mhz();
    return take_reset_cause();
}

void board_heater_write(bool on)
{
    PIN_HEATER = on;
}

void board_watchdog_init(void)
{
    TA_WRITE(WDCON, (WDCON & (uint8_t)~(WDCON_WDPS_MASK | WDCON_WDTRF)) | WDCON_WDPS_410MS);
    board_watchdog_feed();
}

void board_watchdog_feed(void)
{
    TA_SET_BITS(WDCON, WDCON_WDCLR);
}

bool board_watchdog_from_config(void)
{
    /* CONFIG4[7:4] WDTEN: 1111 = disabled, anything else = reset timer. */
    return (iap_read(IAP_READ_CONFIG, 4) & 0xF0) != 0xF0;
}
