#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ble_client.h"

typedef enum {
    PATTERN_RAMP = 0,
    PATTERN_RANDOM,
    PATTERN_PULSE,
    PATTERN_COUNT,
} pattern_id_t;

typedef struct {
    pattern_id_t selected;
    bool running;
    bool motor_enabled[MOTOR_COUNT];
    uint8_t intensity;
} pattern_snapshot_t;

void pattern_control_init(void);
void pattern_control_select(pattern_id_t pattern);
void pattern_control_set_running(bool running);
void pattern_control_set_motor_enabled(motor_id_t motor, bool enabled);
void pattern_control_get_snapshot(pattern_snapshot_t *snapshot);
