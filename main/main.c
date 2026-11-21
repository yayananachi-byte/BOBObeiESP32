#include "app_settings.h"
#include "ble_client.h"
#include "imu_game.h"
#include "power_status.h"
#include "pattern_control.h"
#include "ui.h"

#include "esp_err.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "sosexy_remote";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    app_settings_init();

    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    ret = power_status_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor unavailable: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Creating user interface");
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    /* Serialize LCD commands with the adapter's SPI display refreshes. */
    app_settings_t settings;
    app_settings_get(&settings);
    ESP_ERROR_CHECK(bsp_display_brightness_set(settings.brightness));
    app_ui_create();
    bsp_display_unlock();
    ESP_LOGI(TAG, "User interface ready");

    imu_game_init();
    ble_client_init();
    pattern_control_init();
    ESP_LOGI(TAG, "SOSEXY remote started");
}
