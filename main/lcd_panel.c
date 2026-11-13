#include "lcd_panel.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd";

/* ---- Pin map (from Waveshare's ESP32-S3-LCD-Driver-Board 2.1" demo) ---- */
#define PIN_SPI_SDA 1
#define PIN_SPI_SCL 2
#define PIN_SPI_CS 42

#define PIN_HSYNC 38
#define PIN_VSYNC 39
#define PIN_DE 40
#define PIN_PCLK 41
#define PIN_BACKLIGHT 6 /* active low on this board */

/* RGB565: D0..D4 = B0..B4, D5..D10 = G0..G5, D11..D15 = R0..R4 */
static const int rgb_data_pins[16] = {
    5, 45, 48, 47, 21,     /* B0..B4 */
    14, 13, 12, 11, 10, 9, /* G0..G5 */
    46, 3, 8, 18, 17,      /* R0..R4 */
};

#define LCD_PCLK_HZ (14 * 1000 * 1000)

/* ---- ST7701S init sequence for the Waveshare 2.1" round panel ---- */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint16_t delay_ms;
    uint8_t data[16];
} st7701_cmd_t;

static const st7701_cmd_t st7701_init_2_1in[] = {
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x10}}, /* Command2 BK0 */
    {0xC0, 2, 0, {0x3B, 0x00}},
    {0xC1, 2, 0, {0x0B, 0x02}},
    {0xC2, 2, 0, {0x07, 0x02}},
    {0xCC, 1, 0, {0x10}},
    {0xCD, 1, 0, {0x08}},
    {0xB0, 16, 0, {0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}},
    {0xB1, 16, 0, {0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x11}}, /* Command2 BK1 */
    {0xB0, 1, 0, {0x6D}},
    {0xB1, 1, 0, {0x37}},
    {0xB2, 1, 0, {0x81}},
    {0xB3, 1, 0, {0x80}},
    {0xB5, 1, 0, {0x43}},
    {0xB7, 1, 0, {0x85}},
    {0xB8, 1, 0, {0x20}},
    {0xC1, 1, 0, {0x78}},
    {0xC2, 1, 0, {0x78}},
    {0xD0, 1, 0, {0x88}},
    {0xE0, 3, 0, {0x00, 0x00, 0x02}},
    {0xE1, 11, 0, {0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}},
    {0xE2, 13, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xE3, 4, 0, {0x00, 0x00, 0x11, 0x00}},
    {0xE4, 2, 0, {0x22, 0x00}},
    {0xE5, 16, 0, {0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xE6, 4, 0, {0x00, 0x00, 0x11, 0x00}},
    {0xE7, 2, 0, {0x22, 0x00}},
    {0xE8, 16, 0, {0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xEB, 7, 0, {0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}},
    {0xED, 16, 0, {0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}},
    {0xEF, 6, 0, {0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x13}}, /* Command2 BK3 */
    {0xEF, 1, 0, {0x08}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x00}}, /* back to Command1 */
    {0x36, 1, 0, {0x00}},                         /* MADCTL */
    {0x3A, 1, 0, {0x66}},                         /* COLMOD: 18-bit */
    {0x11, 0, 480, {0}},                          /* Sleep out */
    {0x20, 0, 120, {0}},                          /* Inversion off */
    {0x29, 0, 0, {0}},                            /* Display on */
};

/* 3-wire SPI = 9-bit words: 1 D/C bit followed by 8 data bits.
 * Sent as a 1-bit "command" phase (D/C) + 8-bit "address" phase (payload). */
static void st7701_write(spi_device_handle_t spi, bool is_data, uint8_t value)
{
    spi_transaction_t t = {
        .cmd = is_data ? 1 : 0,
        .addr = value,
        .length = 0,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

static void st7701_send_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SPI_SDA,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SPI_SCL,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev = {
        .command_bits = 1,
        .address_bits = 8,
        .clock_speed_hz = 5 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_SPI_CS,
        .queue_size = 1,
    };
    spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &spi));

    for (size_t i = 0; i < sizeof(st7701_init_2_1in) / sizeof(st7701_init_2_1in[0]); i++) {
        const st7701_cmd_t *c = &st7701_init_2_1in[i];
        st7701_write(spi, false, c->cmd);
        for (int j = 0; j < c->len; j++) {
            st7701_write(spi, true, c->data[j]);
        }
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }

    /* The SPI link is only needed for init; release it. */
    spi_bus_remove_device(spi);
    spi_bus_free(SPI3_HOST);
}

esp_lcd_panel_handle_t lcd_panel_init(int num_fbs)
{
    gpio_config_t bl = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_BACKLIGHT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_BACKLIGHT, 1); /* off while initialising */

    ESP_LOGI(TAG, "Sending ST7701S init sequence");
    st7701_send_init();

    ESP_LOGI(TAG, "Creating RGB panel");
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .vsync_pulse_width = 3,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = false,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = num_fbs,
        .bounce_buffer_size_px = LCD_H_RES * 20,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_HSYNC,
        .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = PIN_DE,
        .pclk_gpio_num = PIN_PCLK,
        .disp_gpio_num = -1,
        .flags.fb_in_psram = 1,
    };
    for (int i = 0; i < 16; i++) {
        cfg.data_gpio_nums[i] = rgb_data_pins[i];
    }

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    gpio_set_level(PIN_BACKLIGHT, 0); /* backlight on */
    return panel;
}
