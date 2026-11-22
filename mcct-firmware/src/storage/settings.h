/*
 * Persistent settings.
 *
 * Stored as 16-byte records { sequence, payload, CRC-8 } appended round-robin
 * through 16 slots in two flash pages, so a page is erased only every 8 saves
 * and a power cut mid-write leaves the previous record intact (its CRC still
 * matches; the torn one's does not). The newest valid record wins at boot.
 * Changing the layout of settings_t: bump SETTINGS_VERSION (it seeds the CRC,
 * so old records are ignored and defaults load).
 */
#ifndef STORAGE_SETTINGS_H
#define STORAGE_SETTINGS_H

#include "common/types.h"

#define SETTINGS_VERSION    1

typedef struct {
    int16_t  target_c;          /* constant-temperature target */
    int8_t   offset_c;          /* added to the calibrated reading */
    uint8_t  profile;           /* last chosen reflow profile, 0-based */
    uint16_t kp_x10;            /* PID: tenths of %/degC */
    uint16_t ti_s;
    uint16_t td_s;
    uint16_t loss_x1000;        /* thermal model, see thermal_model.h */
    uint16_t cap_x10;
} settings_t;

extern __xdata settings_t settings;

void settings_defaults(void);
void settings_load(void);           /* newest valid record, or defaults */
void settings_changed(void);        /* schedule a save */
void settings_service(void);        /* saves CFG_SAVE_DELAY_MS after the last change */
void settings_save(void);           /* write now */

#endif
