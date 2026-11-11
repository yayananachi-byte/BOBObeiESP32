#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float ball_x;
    float ball_y;
    uint8_t intensity;
    bool running;
    bool calibrating;
    bool imu_ok;
    bool motor_enabled[3];
} imu_game_snapshot_t;

typedef struct {
    float acceleration;
    uint8_t intensity;
    bool running;
    bool moving;
    bool imu_ok;
    bool motor_enabled[3];
} imu_motion_snapshot_t;

void imu_game_init(void);
void imu_game_set_running(bool running);
void imu_game_set_motor_enabled(int motor, bool enabled);
void imu_game_request_reset(void);
void imu_game_get_snapshot(imu_game_snapshot_t *snapshot);
void imu_motion_set_running(bool running);
void imu_motion_set_motor_enabled(int motor, bool enabled);
void imu_motion_get_snapshot(imu_motion_snapshot_t *snapshot);
