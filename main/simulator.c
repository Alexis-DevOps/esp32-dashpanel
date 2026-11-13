#include "simulator.h"

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vehicle_data.h"

#define TICK_MS 20
#define IDLE_RPM 800.0f

static volatile bool s_enabled = true;

typedef enum { ST_IDLE, ST_ACCEL, ST_CRUISE, ST_BRAKE } sim_phase_t;

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)esp_random() / (float)UINT32_MAX);
}

/* km/h gained per second at full-ish throttle, per gear */
static const float accel_kmh_s[7] = {0, 11.0f, 8.0f, 6.0f, 4.5f, 3.2f, 2.2f};

static void sim_task(void *arg)
{
    sim_phase_t phase = ST_IDLE;
    float speed = 0, rpm = IDLE_RPM, phase_time = 0;
    float target_speed = 0, shift_rpm = 0, cruise_time = 0;
    int gear = 0;
    float fuel = 62.0f;
    const float dt = TICK_MS / 1000.0f;

    for (;;) {
        phase_time += dt;

        switch (phase) {
        case ST_IDLE:
            gear = 0;
            speed = 0;
            rpm = IDLE_RPM + frand(-25, 25);
            if (phase_time > 2.5f) {
                target_speed = frand(60, 190);
                shift_rpm = frand(3200, 6900); /* sometimes cruisy, sometimes spirited */
                cruise_time = frand(2, 5);
                gear = 1;
                phase = ST_ACCEL;
                phase_time = 0;
            }
            break;

        case ST_ACCEL:
            speed += accel_kmh_s[gear] * (shift_rpm / 6000.0f) * dt;
            rpm = speed * vehicle_gear_ratio[gear];
            if (gear == 1 && rpm < 1400) {
                rpm = 1400 + frand(-20, 20); /* clutch slip on launch */
            }
            if (rpm >= shift_rpm && gear < VEHICLE_GEAR_COUNT) {
                gear++;
                rpm = speed * vehicle_gear_ratio[gear];
            }
            if (rpm > 7600) {
                rpm = 7600; /* limiter */
                speed = rpm / vehicle_gear_ratio[gear];
            }
            if (speed >= target_speed) {
                phase = ST_CRUISE;
                phase_time = 0;
            }
            break;

        case ST_CRUISE:
            /* settle into the highest gear that keeps > 1700 rpm */
            while (gear < VEHICLE_GEAR_COUNT && speed * vehicle_gear_ratio[gear + 1] > 1700) {
                gear++;
            }
            speed += frand(-0.3f, 0.3f);
            rpm = speed * vehicle_gear_ratio[gear];
            if (phase_time > cruise_time) {
                phase = ST_BRAKE;
                phase_time = 0;
            }
            break;

        case ST_BRAKE:
            speed -= 14.0f * dt;
            if (gear > 1 && speed * vehicle_gear_ratio[gear] < 1300) {
                gear--;
            }
            rpm = speed * vehicle_gear_ratio[gear];
            if (speed < 8) {
                gear = 0; /* clutch in */
            }
            if (gear == 0 || rpm < IDLE_RPM) {
                rpm = IDLE_RPM;
            }
            if (speed <= 0) {
                speed = 0;
                phase = ST_IDLE;
                phase_time = 0;
            }
            break;
        }

        /* burn rate grows with rpm; exaggerated so the gauge visibly moves. Refill when empty. */
        fuel -= rpm * 0.00000012f * TICK_MS;
        if (fuel <= 0) {
            fuel = 100.0f;
        }

        vehicle_state_t s = {
            .rpm = (int)rpm,
            .speed_kmh = (int)(speed + 0.5f),
            .gear = gear,
            .fuel_pct = (int)(fuel + 0.5f),
            .rpm_max = VEHICLE_DEFAULT_RPM_MAX,
        };
        if (s_enabled) {
            vehicle_state_set(&s);
        }
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void simulator_start(void)
{
    xTaskCreate(sim_task, "sim", 3072, NULL, 3, NULL);
}

void simulator_set_enabled(bool enabled)
{
    s_enabled = enabled;
}
