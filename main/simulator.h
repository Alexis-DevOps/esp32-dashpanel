#pragma once

#include <stdbool.h>

/** Start a task that fakes a car driving (accelerate, shift, cruise, brake, idle) into vehicle_state. */
void simulator_start(void);

/** Pause/resume writing to vehicle_state (used while SimHub is feeding real data). */
void simulator_set_enabled(bool enabled);
