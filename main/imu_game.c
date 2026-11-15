#include "imu_game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_settings.h"
#include "ble_client.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef M_PI
#undef M_PI
#endif
#include "qmi8658.h"

static const char *TAG = "balance_game";
static portMUX_TYPE s_game_lock = portMUX_INITIALIZER_UNLOCKED;
static imu_game_snapshot_t s_snapshot = {
    .motor_enabled = {true, true, false},
};
static imu_motion_snapshot_t s_motion = {
    .motor_enabled = {true, true, false},
};
static volatile bool s_reset_requested = true;
static volatile bool s_motion_filter_reset = true;
static qmi8658_dev_t s_imu;
static float s_bias_x;
static float s_bias_y;

static float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static void IRAM_ATTR boot_button_isr(void *arg)
{
    (void)arg;
    s_reset_requested = true;
}

static void calibrate_imu(void)
{
    qmi8658_data_t data;
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    int samples = 0;

    portENTER_CRITICAL(&s_game_lock);
    s_snapshot.calibrating = true;
    s_snapshot.ball_x = 0;
    s_snapshot.ball_y = 0;
    s_snapshot.intensity = 0;
    portEXIT_CRITICAL(&s_game_lock);
    ble_client_clear_source(BLE_CONTROL_GAME);

    for (int i = 0; i < 80; ++i) {
        bool ready = false;
        if (qmi8658_is_data_ready(&s_imu, &ready) == ESP_OK && ready &&
            qmi8658_read_sensor_data(&s_imu, &data) == ESP_OK) {
            sum_x += data.accelX;
            sum_y += data.accelY;
            ++samples;
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }

    if (samples > 0) {
        s_bias_x = sum_x / samples;
        s_bias_y = sum_y / samples;
        ESP_LOGI(TAG, "Neutral reset: x=%.3f y=%.3f", s_bias_x, s_bias_y);
    } else {
        ESP_LOGW(TAG, "Neutral reset had no IMU samples");
    }

    portENTER_CRITICAL(&s_game_lock);
    s_snapshot.calibrating = false;
    portEXIT_CRITICAL(&s_game_lock);
}

static void game_task(void *arg)
{
    (void)arg;
    qmi8658_data_t data;
    float filtered_x = 0.0f;
    float filtered_y = 0.0f;
    uint8_t last_sent = 255;
    TickType_t last_send_tick = 0;
    float gravity_x = 0.0f;
    float gravity_y = 0.0f;
    float gravity_z = 0.0f;
    float motion_accel = 0.0f;
    uint8_t motion_last_sent[MOTOR_COUNT] = {0};
    bool gravity_ready = false;
    bool motion_output_active[MOTOR_COUNT] = {false};
    TickType_t motion_last_send_tick = 0;

    s_reset_requested = false;
    calibrate_imu();

    while (true) {
        if (s_reset_requested) {
            s_reset_requested = false;
            calibrate_imu();
            filtered_x = 0;
            filtered_y = 0;
            last_sent = 255;
        }

        bool ready = false;
        if (qmi8658_is_data_ready(&s_imu, &ready) == ESP_OK && ready &&
            qmi8658_read_sensor_data(&s_imu, &data) == ESP_OK) {
            if (s_motion_filter_reset || !gravity_ready) {
                s_motion_filter_reset = false;
                gravity_x = data.accelX;
                gravity_y = data.accelY;
                gravity_z = data.accelZ;
                motion_accel = 0.0f;
                memset(motion_last_sent, 0, sizeof(motion_last_sent));
                memset(motion_output_active, 0, sizeof(motion_output_active));
                gravity_ready = true;
            }

            /* A slow low-pass estimate tracks gravity. The remainder is
             * linear acceleration caused by moving the controller. */
            gravity_x = gravity_x * 0.96f + data.accelX * 0.04f;
            gravity_y = gravity_y * 0.96f + data.accelY * 0.04f;
            gravity_z = gravity_z * 0.96f + data.accelZ * 0.04f;
            float linear_x = data.accelX - gravity_x;
            float linear_y = data.accelY - gravity_y;
            float linear_z = data.accelZ - gravity_z;
            float linear_magnitude = sqrtf(linear_x * linear_x +
                                           linear_y * linear_y +
                                           linear_z * linear_z);
            motion_accel = motion_accel * 0.70f + linear_magnitude * 0.30f;

            app_settings_t settings;
            app_settings_get(&settings);

            const float motion_dead_zone = 0.35f;
            const float motion_full_scale = 7.0f;
            float adjusted_motion_accel = motion_accel *
                ((float)settings.motion_sensitivity / 100.0f);
            float motion_ratio = (adjusted_motion_accel - motion_dead_zone) /
                                 (motion_full_scale - motion_dead_zone);
            uint8_t motion_intensity = (uint8_t)lroundf(
                clampf(motion_ratio, 0.0f, 1.0f) * 100.0f);
            if (motion_intensity < 3) {
                motion_intensity = 0;
            }

            float dx = data.accelX - s_bias_x;
            float dy = data.accelY - s_bias_y;
            filtered_x = filtered_x * 0.72f + dx * 0.28f;
            filtered_y = filtered_y * 0.72f + dy * 0.28f;

            float game_gain = (float)settings.ball_sensitivity / 100.0f;
            float adjusted_x = filtered_x * game_gain;
            float adjusted_y = filtered_y * game_gain;
            float magnitude = sqrtf(adjusted_x * adjusted_x + adjusted_y * adjusted_y);
            float angle_deg = atan2f(magnitude, 9.80665f) * 57.2957795f;
            uint8_t intensity = (uint8_t)lroundf(clampf(angle_deg / 45.0f, 0.0f, 1.0f) * 100.0f);
            if (intensity > settings.ball_limit) {
                intensity = settings.ball_limit;
            }
            float ball_limit = (float)settings.ball_limit / 100.0f;

            imu_game_snapshot_t local;
            imu_motion_snapshot_t motion_local;
            portENTER_CRITICAL(&s_game_lock);
            s_snapshot.ball_x = clampf(-adjusted_x / 7.0f,
                                       -ball_limit, ball_limit);
            s_snapshot.ball_y = clampf(adjusted_y / 7.0f,
                                       -ball_limit, ball_limit);
            s_snapshot.intensity = s_snapshot.running ? intensity : 0;
            local = s_snapshot;
            s_motion.acceleration = motion_accel;
            s_motion.intensity = s_motion.running ? motion_intensity : 0;
            s_motion.moving = s_motion.running && motion_intensity > 0;
            motion_local = s_motion;
            portEXIT_CRITICAL(&s_game_lock);

            TickType_t now = xTaskGetTickCount();
            int delta = (int)intensity - (int)last_sent;
            if (delta < 0) {
                delta = -delta;
            }
            if (local.running &&
                (last_sent == 255 || delta >= 2 || now - last_send_tick >= pdMS_TO_TICKS(120))) {
                for (int motor = 0; motor < MOTOR_COUNT; ++motor) {
                    ble_client_request_motor_from(BLE_CONTROL_GAME,
                        (motor_id_t)motor,
                        local.motor_enabled[motor] ? intensity : 0);
                }
                last_sent = intensity;
                last_send_tick = now;
            }

            bool periodic_motion_update =
                now - motion_last_send_tick >= pdMS_TO_TICKS(90);
            bool sent_motion_update = false;
            for (int motor = 0; motor < MOTOR_COUNT; ++motor) {
                int motion_delta = (int)motion_intensity -
                                   (int)motion_last_sent[motor];
                if (motion_delta < 0) {
                    motion_delta = -motion_delta;
                }
                bool should_run = motion_local.running &&
                                  motion_local.motor_enabled[motor] &&
                                  motion_intensity > 0;
                if (should_run &&
                    (!motion_output_active[motor] || motion_delta >= 2 ||
                     periodic_motion_update)) {
                    ble_client_request_motor_from(BLE_CONTROL_MOTION,
                        (motor_id_t)motor, motion_intensity);
                    motion_last_sent[motor] = motion_intensity;
                    motion_output_active[motor] = true;
                    sent_motion_update = true;
                } else if (!should_run && motion_output_active[motor]) {
                    /* Send one zero when movement ends or a channel is
                     * disabled, then remain silent while it stays still. */
                    ble_client_request_motor_from(BLE_CONTROL_MOTION,
                        (motor_id_t)motor, 0);
                    motion_last_sent[motor] = 0;
                    motion_output_active[motor] = false;
                    sent_motion_update = true;
                }
            }
            if (sent_motion_update) {
                motion_last_send_tick = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void imu_game_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    esp_err_t ret = qmi8658_init(&s_imu, bus, QMI8658_ADDRESS_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 init failed: %s", esp_err_to_name(ret));
        return;
    }

    qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G);
    qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_500HZ);
    qmi8658_set_accel_unit_mps2(&s_imu, true);
    qmi8658_write_register(&s_imu, QMI8658_CTRL5, 0x03);

    portENTER_CRITICAL(&s_game_lock);
    s_snapshot.imu_ok = true;
    s_motion.imu_ok = true;
    portEXIT_CRITICAL(&s_game_lock);

    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));
    /* The BSP touch driver has already installed the shared GPIO ISR service. */
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_NUM_0, boot_button_isr, NULL));

    xTaskCreate(game_task, "imu_game", 6144, NULL, 4, NULL);
}

void imu_game_set_running(bool running)
{
    portENTER_CRITICAL(&s_game_lock);
    s_snapshot.running = running;
    if (!running) {
        s_snapshot.intensity = 0;
    } else {
        s_snapshot.motor_enabled[MOTOR_SUCTION] = true;
        s_snapshot.motor_enabled[MOTOR_VIBRATION] = true;
        s_snapshot.motor_enabled[MOTOR_ELECTRIC] = false;
    }
    portEXIT_CRITICAL(&s_game_lock);

    if (running) {
        s_reset_requested = true;
    } else {
        ble_client_clear_source(BLE_CONTROL_GAME);
    }
}

void imu_motion_set_running(bool running)
{
    portENTER_CRITICAL(&s_game_lock);
    s_motion.running = running;
    s_motion.intensity = 0;
    s_motion.moving = false;
    portEXIT_CRITICAL(&s_game_lock);

    if (running) {
        s_motion_filter_reset = true;
    } else {
        ble_client_clear_source(BLE_CONTROL_MOTION);
    }
}

void imu_motion_set_motor_enabled(int motor, bool enabled)
{
    if (motor < 0 || motor >= MOTOR_COUNT) {
        return;
    }
    portENTER_CRITICAL(&s_game_lock);
    s_motion.motor_enabled[motor] = enabled;
    portEXIT_CRITICAL(&s_game_lock);
    if (!enabled) {
        ble_client_request_motor_from(BLE_CONTROL_MOTION,
                                      (motor_id_t)motor, 0);
    }
}

void imu_game_set_motor_enabled(int motor, bool enabled)
{
    if (motor < 0 || motor >= MOTOR_COUNT) {
        return;
    }
    portENTER_CRITICAL(&s_game_lock);
    s_snapshot.motor_enabled[motor] = enabled;
    portEXIT_CRITICAL(&s_game_lock);
    if (!enabled) {
        ble_client_request_motor_from(BLE_CONTROL_GAME,
                                      (motor_id_t)motor, 0);
    }
}

void imu_game_request_reset(void)
{
    s_reset_requested = true;
}

void imu_game_get_snapshot(imu_game_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_game_lock);
    *snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_game_lock);
}

void imu_motion_get_snapshot(imu_motion_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_game_lock);
    *snapshot = s_motion;
    portEXIT_CRITICAL(&s_game_lock);
}
