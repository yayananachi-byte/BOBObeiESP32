idf_component_register(
    SRCS
        "main.c"
        "ble_client.c"
        "imu_game.c"
        "ui.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_driver_gpio
)
