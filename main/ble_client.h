#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLE_MAX_SCAN_RESULTS 16

typedef enum {
    MOTOR_SUCTION = 0,
    MOTOR_VIBRATION,
    MOTOR_ELECTRIC,
    MOTOR_COUNT,
} motor_id_t;

typedef enum {
    BLE_CONTROL_MANUAL = 0,
    BLE_CONTROL_GAME,
    BLE_CONTROL_MOTION,
    BLE_CONTROL_PATTERN,
    BLE_CONTROL_SOURCE_COUNT,
} ble_control_source_t;

typedef enum {
    BLE_APP_STARTING = 0,
    BLE_APP_IDLE,
    BLE_APP_SCANNING,
    BLE_APP_CONNECTING,
    BLE_APP_DISCOVERING,
    BLE_APP_READY,
    BLE_APP_ERROR,
} ble_app_state_t;

typedef struct {
    char name[32];
    char address[18];
    int8_t rssi;
} ble_scan_result_t;

void ble_client_init(void);
bool ble_client_start_scan(void);
bool ble_client_connect_index(size_t index);
size_t ble_client_get_results(ble_scan_result_t *out, size_t capacity, uint32_t *generation);
ble_app_state_t ble_client_get_state(char *peer_name, size_t peer_name_size);
bool ble_client_is_ready(void);
void ble_client_request_motor(motor_id_t motor, uint8_t intensity);
void ble_client_request_motor_from(ble_control_source_t source,
                                   motor_id_t motor, uint8_t intensity);
void ble_client_clear_source(ble_control_source_t source);
void ble_client_stop_all(void);
