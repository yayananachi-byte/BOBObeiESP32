#include "power_status.h"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2101_ADDRESS       0x34
#define AXP2101_STATUS1       0x00
#define AXP2101_STATUS2       0x01
#define AXP2101_BAT_PERCENT   0xA4

static const char *TAG = "power_status";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static i2c_master_dev_handle_t s_pmu;
static power_status_snapshot_t s_snapshot;

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_pmu, &reg, 1, value, 1, 100);
}

static void power_status_task(void *arg)
{
    (void)arg;
    while (true) {
        uint8_t status1;
        uint8_t status2;
        uint8_t percent;
        esp_err_t err = read_register(AXP2101_STATUS1, &status1);
        if (err == ESP_OK) err = read_register(AXP2101_STATUS2, &status2);
        if (err == ESP_OK) err = read_register(AXP2101_BAT_PERCENT, &percent);

        portENTER_CRITICAL(&s_lock);
        s_snapshot.available = err == ESP_OK;
        if (err == ESP_OK) {
            s_snapshot.battery_connected = (status1 & (1U << 3)) != 0;
            s_snapshot.charging = (status2 >> 5) == 0x01;
            s_snapshot.percent = percent <= 100 ? percent : 100;
        }
        portEXIT_CRITICAL(&s_lock);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AXP2101 read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t power_status_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDRESS,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &s_pmu);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to add AXP2101: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(power_status_task, "power_status", 3072, NULL, 3, NULL) != pdPASS) {
        i2c_master_bus_rm_device(s_pmu);
        s_pmu = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void power_status_get(power_status_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
}
