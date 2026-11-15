#include "app_settings.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define BALL_SENSITIVITY_MIN   50
#define BALL_SENSITIVITY_MAX  200
#define BALL_LIMIT_MIN          20
#define BALL_LIMIT_MAX         100
#define MOTION_SENSITIVITY_MIN  50
#define MOTION_SENSITIVITY_MAX 200
#define BRIGHTNESS_MIN           10
#define BRIGHTNESS_MAX          100

static const char *TAG = "app_settings";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_settings_t s_settings = {
    .ball_sensitivity = 100,
    .ball_limit = 100,
    .motion_sensitivity = 100,
    .brightness = 100,
};

static uint16_t clamp_u16(uint16_t value, uint16_t low, uint16_t high)
{
    return value < low ? low : (value > high ? high : value);
}

static uint8_t clamp_u8(uint8_t value, uint8_t low, uint8_t high)
{
    return value < low ? low : (value > high ? high : value);
}

void app_settings_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved settings; using defaults");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open settings: %s", esp_err_to_name(err));
        return;
    }

    app_settings_t loaded = s_settings;
    uint16_t value16;
    uint8_t value8;
    if (nvs_get_u16(handle, "ball_sens", &value16) == ESP_OK) {
        loaded.ball_sensitivity = clamp_u16(value16,
            BALL_SENSITIVITY_MIN, BALL_SENSITIVITY_MAX);
    }
    if (nvs_get_u8(handle, "ball_limit", &value8) == ESP_OK) {
        loaded.ball_limit = clamp_u8(value8, BALL_LIMIT_MIN, BALL_LIMIT_MAX);
    }
    if (nvs_get_u16(handle, "motion_sens", &value16) == ESP_OK) {
        loaded.motion_sensitivity = clamp_u16(value16,
            MOTION_SENSITIVITY_MIN, MOTION_SENSITIVITY_MAX);
    }
    if (nvs_get_u8(handle, "brightness", &value8) == ESP_OK) {
        loaded.brightness = clamp_u8(value8, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    }
    nvs_close(handle);

    portENTER_CRITICAL(&s_lock);
    s_settings = loaded;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Loaded: ball=%u%% limit=%u%% motion=%u%% brightness=%u%%",
             loaded.ball_sensitivity, loaded.ball_limit,
             loaded.motion_sensitivity, loaded.brightness);
}

void app_settings_get(app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *settings = s_settings;
    portEXIT_CRITICAL(&s_lock);
}

void app_settings_set_ball_sensitivity(uint16_t value)
{
    portENTER_CRITICAL(&s_lock);
    s_settings.ball_sensitivity = clamp_u16(value,
        BALL_SENSITIVITY_MIN, BALL_SENSITIVITY_MAX);
    portEXIT_CRITICAL(&s_lock);
}

void app_settings_set_ball_limit(uint8_t value)
{
    portENTER_CRITICAL(&s_lock);
    s_settings.ball_limit = clamp_u8(value, BALL_LIMIT_MIN, BALL_LIMIT_MAX);
    portEXIT_CRITICAL(&s_lock);
}

void app_settings_set_motion_sensitivity(uint16_t value)
{
    portENTER_CRITICAL(&s_lock);
    s_settings.motion_sensitivity = clamp_u16(value,
        MOTION_SENSITIVITY_MIN, MOTION_SENSITIVITY_MAX);
    portEXIT_CRITICAL(&s_lock);
}

void app_settings_set_brightness(uint8_t value)
{
    portENTER_CRITICAL(&s_lock);
    s_settings.brightness = clamp_u8(value, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    portEXIT_CRITICAL(&s_lock);
}

void app_settings_save(void)
{
    app_settings_t settings;
    app_settings_get(&settings);

    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to save settings: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_u16(handle, "ball_sens", settings.ball_sensitivity);
    if (err == ESP_OK) err = nvs_set_u8(handle, "ball_limit", settings.ball_limit);
    if (err == ESP_OK) err = nvs_set_u16(handle, "motion_sens", settings.motion_sensitivity);
    if (err == ESP_OK) err = nvs_set_u8(handle, "brightness", settings.brightness);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings commit failed: %s", esp_err_to_name(err));
    }
}
