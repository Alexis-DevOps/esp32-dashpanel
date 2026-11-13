idf_component_register(
    SRCS "main.c" "lcd_panel.c" "gauge_ui.c" "vehicle_data.c" "simhub.c" "logo_img.c" "digits_img.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_timer esp_psram esp_driver_usb_serial_jtag)
