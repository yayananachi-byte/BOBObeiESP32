#pragma once

#include <stdint.h>

typedef struct {
    uint16_t ball_sensitivity;
    uint8_t ball_limit;
    uint16_t motion_sensitivity;
    uint8_t brightness;
} app_settings_t;

void app_settings_init(void);
void app_settings_get(app_settings_t *settings);
void app_settings_set_ball_sensitivity(uint16_t value);
void app_settings_set_ball_limit(uint8_t value);
void app_settings_set_motion_sensitivity(uint16_t value);
void app_settings_set_brightness(uint8_t value);
void app_settings_save(void);
