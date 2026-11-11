#include "ble_client.h"
#include "imu_game.h"
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

    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    bsp_display_backlight_on();

    ESP_ERROR_CHECK(bsp_display_lock(-1));
    app_ui_create();
    bsp_display_unlock();

    imu_game_init();
    ble_client_init();
    ESP_LOGI(TAG, "SOSEXY remote started");
}
