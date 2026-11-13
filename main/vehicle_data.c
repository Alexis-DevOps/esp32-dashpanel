#include "vehicle_data.h"

#include <math.h>

#include "freertos/FreeRTOS.h"

static vehicle_state_t s_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* Typical 6-speed hatchback: e.g. 3000 rpm ≈ 25 km/h in 1st, ≈ 145 km/h in 6th. */
const float vehicle_gear_ratio[7] = {0.0f, 120.0f, 67.0f, 43.0f, 32.0f, 25.0f, 20.7f};

void vehicle_state_set(const vehicle_state_t *s)
{
    portENTER_CRITICAL(&s_lock);
    s_state = *s;
    portEXIT_CRITICAL(&s_lock);
}

void vehicle_state_get(vehicle_state_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_lock);
}

int vehicle_estimate_gear(int rpm, int speed_kmh)
{
    if (speed_kmh < 3 || rpm < 600) {
        return 0;
    }
    float ratio = (float)rpm / (float)speed_kmh;
    int best = 0;
    float best_err = 1e9f;
    for (int g = 1; g <= VEHICLE_GEAR_COUNT; g++) {
        float err = fabsf(ratio - vehicle_gear_ratio[g]) / vehicle_gear_ratio[g];
        if (err < best_err) {
            best_err = err;
            best = g;
        }
    }
    return best_err < 0.15f ? best : 0;
}
