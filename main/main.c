#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "gauge_ui.h"
#include "lcd_panel.h"
#include "simhub.h"

static const char *TAG = "main";

/*
 * Render mode
 *  1 = PARTIAL: LVGL draws only changed areas into small internal-RAM DMA buffers, then copies
 *      them into a single PSRAM frame buffer. Fast rendering, tiny chance of tearing.
 *  0 = DIRECT:  LVGL draws straight into two PSRAM frame buffers, swapped on VSYNC.
 *      Tear-free, but every pixel is rendered in (slow) PSRAM.
 */
#define RENDER_PARTIAL 1
#define PARTIAL_BUF_LINES 40 /* 480 x 40 px x 2 bytes = 38 KB per buffer, x2 buffers */

void app_main(void)
{
    esp_lcd_panel_handle_t panel = lcd_panel_init(RENDER_PARTIAL ? 1 : 2);

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    lvgl_cfg.task_affinity = 1; /* keep rendering off core 0 (future Wi-Fi/BT for the ELM327) */
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
#if RENDER_PARTIAL
        .buffer_size = LCD_H_RES * PARTIAL_BUF_LINES,
#else
        .buffer_size = LCD_H_RES * LCD_V_RES,
#endif
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
#if RENDER_PARTIAL
            .buff_dma = true,
#else
            .buff_spiram = true,
            .direct_mode = true,
#endif
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = !RENDER_PARTIAL,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    assert(disp);

    lvgl_port_lock(0);
    gauge_ui_show_splash();
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(3000));

    lvgl_port_lock(0);
    gauge_ui_create();
    lvgl_port_unlock();

    /* Telemetry comes from SimHub over USB (see simhub.h); until then the gauge shows NOT CONNECTED */
    simhub_start();

    ESP_LOGI(TAG, "Gauge running");
}
