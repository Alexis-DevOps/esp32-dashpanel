#include "simhub.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vehicle_data.h"

#define SIMHUB_TIMEOUT_MS 2000
#define LINE_MAX_LEN 96

static const char *TAG = "simhub";

static int parse_gear(const char *v)
{
    while (*v == ' ') {
        v++;
    }
    if (*v == 'R' || *v == 'r') {
        return -1;
    }
    if (*v == 'N' || *v == 'n' || *v == '\0') {
        return 0;
    }
    int g = atoi(v);
    if (g < 0) {
        g = 0;
    }
    return g > 9 ? 9 : g; /* SimHub cars can have more than 6 gears */
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 0 (unknown car) stays 0 = use the default; otherwise keep it within what the scale can draw */
static int rpm_limit(int v)
{
    return v <= 0 ? 0 : clamp(v, 3000, 12000);
}

/* Parse "R:5200,S:143,G:4,F:62" into *s (only the keys present are touched). Returns true if any key was valid. */
static bool parse_line(char *line, vehicle_state_t *s)
{
    bool ok = false;
    char *save = NULL;
    for (char *tok = strtok_r(line, ",;", &save); tok; tok = strtok_r(NULL, ",;", &save)) {
        while (*tok == ' ') {
            tok++;
        }
        if (tok[0] == '\0' || tok[1] != ':') {
            continue;
        }
        const char *val = tok + 2;
        switch (toupper((unsigned char)tok[0])) {
        case 'R': s->rpm = clamp(atoi(val), 0, 20000); ok = true; break;
        case 'S': s->speed_kmh = clamp(atoi(val), 0, 999); ok = true; break;
        case 'G': s->gear = parse_gear(val); ok = true; break;
        case 'F': s->fuel_pct = clamp(atoi(val), 0, 100); ok = true; break;
        case 'M': s->rpm_max = rpm_limit(atoi(val)); ok = true; break;
        case 'L': s->rpm_redline = rpm_limit(atoi(val)); ok = true; break;
        default: break;
        }
    }
    return ok;
}

typedef struct {
    vehicle_state_t state;
    bool connected;
    int64_t last_ok_us;
    int64_t last_warn_us;
} link_t;

/* One complete line: apply it, or (rate-limited) log what was rejected so a wrong SimHub message is easy to spot */
static void handle_line(link_t *lk, char *line)
{
    char raw[LINE_MAX_LEN];
    strlcpy(raw, line, sizeof(raw)); /* parse_line() destroys its input */
    int64_t now = esp_timer_get_time();
    if (parse_line(line, &lk->state)) {
        lk->state.connected = true;
        vehicle_state_set(&lk->state);
        lk->last_ok_us = now;
        if (!lk->connected) {
            lk->connected = true;
            ESP_LOGI(TAG, "SimHub connected");
        }
    } else if (now - lk->last_warn_us > 2000000) {
        lk->last_warn_us = now;
        ESP_LOGW(TAG, "ignored line (expected R:..,S:..,G:..,F:..): \"%.60s\"", raw);
    }
}

static void simhub_task(void *arg)
{
    char line[LINE_MAX_LEN];
    size_t len = 0;
    bool overflow = false;
    link_t lk = {.state = {.rpm_max = VEHICLE_DEFAULT_RPM_MAX}};

    for (;;) {
        uint8_t buf[64];
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(20));

        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (len > 0 && !overflow) {
                    line[len] = '\0';
                    handle_line(&lk, line);
                }
                len = 0;
                overflow = false;
            } else if (len < LINE_MAX_LEN - 1) {
                line[len++] = c;
            } else {
                overflow = true; /* garbage / log noise: drop until next newline */
            }
        }

        /* SimHub sends each message in one write: if the line has gone quiet without a newline
         * (message configured without '\n'), treat it as complete instead of waiting forever. */
        if (n <= 0 && len > 0) {
            if (!overflow) {
                line[len] = '\0';
                handle_line(&lk, line);
            }
            len = 0;
            overflow = false;
        }

        if (lk.connected && esp_timer_get_time() - lk.last_ok_us > SIMHUB_TIMEOUT_MS * 1000LL) {
            lk.connected = false;
            lk.state.connected = false;
            lk.state.rpm = lk.state.speed_kmh = lk.state.gear = 0; /* don't leave stale values on screen */
            vehicle_state_set(&lk.state);
            ESP_LOGW(TAG, "SimHub data lost");
        }
    }
}

void simhub_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 512;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { /* INVALID_STATE = already installed */
        ESP_LOGE(TAG, "USB serial driver install failed: %s", esp_err_to_name(err));
        return;
    }
    /* core 0: rendering is pinned to core 1 */
    xTaskCreatePinnedToCore(simhub_task, "simhub", 3072, NULL, 4, NULL, 0);
}
