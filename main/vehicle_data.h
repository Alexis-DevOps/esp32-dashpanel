#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Single source of truth for what the gauge shows.
 * Today it is written by the simulator; later an ELM327 (OBD-II) task will write it.
 */
typedef struct {
    int rpm;       /* engine speed, rev/min            (OBD PID 0x0C) */
    int speed_kmh; /* vehicle speed, km/h              (OBD PID 0x0D) */
    int gear;      /* -1 = R, 0 = N, 1..6 (not an OBD PID: estimated) */
    int fuel_pct;  /* fuel tank level, 0..100 %       (OBD PID 0x2F) */
    bool connected;  /* true while the data source (SimHub) is sending */
    int rpm_max;     /* top of the rev scale; 0 = default (VEHICLE_DEFAULT_RPM_MAX) */
    int rpm_redline; /* start of the red zone;  0 = derived from rpm_max */
} vehicle_state_t;

#define VEHICLE_DEFAULT_RPM_MAX 8000

void vehicle_state_set(const vehicle_state_t *s);
void vehicle_state_get(vehicle_state_t *out);

/**
 * Guess the engaged gear from rpm / speed.
 * Standard OBD-II has no gear PID, so this is what the ELM327 path will use.
 * Returns 0 (N) when stopped or when the ratio matches no gear (clutch in, coasting).
 */
int vehicle_estimate_gear(int rpm, int speed_kmh);

/* rpm per km/h for each gear (index 1..6). Tune to the real car for the ELM327 path. */
extern const float vehicle_gear_ratio[7];
#define VEHICLE_GEAR_COUNT 6
