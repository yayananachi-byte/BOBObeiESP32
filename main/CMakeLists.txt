idf_component_register(
    SRCS
        "main.c"
        "ble_client.c"
        "imu_game.c"
        "app_settings.c"
        "power_status.c"
        "pattern_control.c"
        "ui.c"
        "fonts/ui_font_16.c"
        "fonts/ui_font_22.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_driver_gpio esp_driver_i2c
)
