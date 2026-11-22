/*
 * Reflow profiles, compiled from the JSON files in profiles/ by scripts/gen_profiles.py
 * into build/generated/profiles_gen.c.
 *
 * A profile is a list of waypoints. For each waypoint the setpoint ramps
 * linearly from where the previous one ended to `temp` over `ramp_s`, the
 * controller then waits until the plate actually reaches it (the display
 * flashes twice), and holds it for `hold_s`.
 */
#ifndef CONTROL_PROFILE_H
#define CONTROL_PROFILE_H

#include "common/types.h"

#define PROFILE_MAX         3
#define PROFILE_LABEL_LEN   3

typedef struct {
    temp_dc_t temp;
    uint16_t  ramp_s;
    uint16_t  hold_s;
} waypoint_t;

typedef struct {
    char    label[PROFILE_LABEL_LEN + 1];   /* shown on the display */
    uint8_t first;                          /* index into profile_waypoints */
    uint8_t count;
} profile_t;

extern const uint8_t __code profile_count;
extern const profile_t __code profiles[];
extern const waypoint_t __code profile_waypoints[];

#endif
