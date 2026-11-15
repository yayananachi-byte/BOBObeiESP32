#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool available;
    bool battery_connected;
    bool charging;
    uint8_t percent;
} power_status_snapshot_t;

esp_err_t power_status_init(void);
void power_status_get(power_status_snapshot_t *snapshot);
