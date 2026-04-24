#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct hal_imu_s hal_imu_t;

typedef struct { float x, y, z; } hal_accel_t;  /* m/s² */
typedef struct { float x, y, z; } hal_gyro_t;   /* deg/s */

struct hal_imu_s {
    esp_err_t (*init)(hal_imu_t *imu);
    esp_err_t (*deinit)(hal_imu_t *imu);
    esp_err_t (*read_accel)(hal_imu_t *imu, hal_accel_t *out);
    esp_err_t (*read_gyro)(hal_imu_t *imu, hal_gyro_t *out);
    esp_err_t (*read_both)(hal_imu_t *imu, hal_accel_t *accel, hal_gyro_t *gyro);
    void      *priv;
};

esp_err_t hal_imu_create(hal_imu_t **out);
void      hal_imu_destroy(hal_imu_t *imu);
