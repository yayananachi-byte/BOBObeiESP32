#include "pattern_control.h"

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static pattern_snapshot_t s_state = {
    .selected = PATTERN_RAMP,
    .motor_enabled = {false, true, false},
};
static uint32_t s_generation;

static void pattern_task(void *arg)
{
    (void)arg;
    uint32_t generation = UINT32_MAX;
    TickType_t cycle_start = 0;
    uint8_t random_value = 0;
    uint8_t last_output[MOTOR_COUNT] = {0};

    while (true) {
        pattern_snapshot_t state;
        uint32_t current_generation;
        portENTER_CRITICAL(&s_lock);
        state = s_state;
        current_generation = s_generation;
        portEXIT_CRITICAL(&s_lock);

        if (current_generation != generation) {
            generation = current_generation;
            cycle_start = xTaskGetTickCount();
            random_value = (uint8_t)((esp_random() % 5U + 1U) * 20U);
            for (int motor = 0; motor < MOTOR_COUNT; ++motor) {
                last_output[motor] = 255;
            }
        }

        if (state.running) {
            uint32_t elapsed_ms = (uint32_t)(xTaskGetTickCount() - cycle_start) *
                                  portTICK_PERIOD_MS;
            uint8_t intensity = 0;
            switch (state.selected) {
            case PATTERN_RAMP:
                /* Leave enough time at 100 for the BLE writer to send it. */
                if (elapsed_ms % 5250U >= 5000U) intensity = 100;
                else intensity = (uint8_t)((elapsed_ms % 5250U) / 50U);
                break;
            case PATTERN_RANDOM:
                if (elapsed_ms >= 5000U) {
                    cycle_start = xTaskGetTickCount();
                    random_value = (uint8_t)((esp_random() % 5U + 1U) * 20U);
                }
                intensity = random_value;
                break;
            case PATTERN_PULSE:
                intensity = (elapsed_ms % 2000U < 1000U) ? 100 : 0;
                break;
            default:
                break;
            }

            portENTER_CRITICAL(&s_lock);
            bool still_running = s_generation == generation && s_state.running;
            if (still_running) {
                s_state.intensity = intensity;
            }
            portEXIT_CRITICAL(&s_lock);
            if (still_running) {
                for (int motor = 0; motor < MOTOR_COUNT; ++motor) {
                    uint8_t output = state.motor_enabled[motor] ? intensity : 0;
                    if (last_output[motor] != output) {
                        ble_client_request_motor_from(BLE_CONTROL_PATTERN,
                            (motor_id_t)motor, output);
                        last_output[motor] = output;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void pattern_control_init(void)
{
    xTaskCreate(pattern_task, "patterns", 3072, NULL, 4, NULL);
}

void pattern_control_select(pattern_id_t pattern)
{
    if (pattern >= PATTERN_COUNT) return;
    portENTER_CRITICAL(&s_lock);
    s_state.selected = pattern;
    s_state.intensity = 0;
    ++s_generation;
    portEXIT_CRITICAL(&s_lock);
    ble_client_clear_source(BLE_CONTROL_PATTERN);
}

void pattern_control_set_running(bool running)
{
    portENTER_CRITICAL(&s_lock);
    s_state.running = running;
    s_state.intensity = 0;
    if (running) {
        s_state.motor_enabled[MOTOR_ELECTRIC] = false;
    }
    ++s_generation;
    portEXIT_CRITICAL(&s_lock);
    ble_client_clear_source(BLE_CONTROL_PATTERN);
}

void pattern_control_set_motor_enabled(motor_id_t motor, bool enabled)
{
    if (motor >= MOTOR_COUNT) return;
    portENTER_CRITICAL(&s_lock);
    s_state.motor_enabled[motor] = enabled;
    portEXIT_CRITICAL(&s_lock);
    if (!enabled) {
        ble_client_request_motor_from(BLE_CONTROL_PATTERN, motor, 0);
    }
}

void pattern_control_get_snapshot(pattern_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    portENTER_CRITICAL(&s_lock);
    *snapshot = s_state;
    portEXIT_CRITICAL(&s_lock);
}
