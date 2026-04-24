#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "icm42670.h"
#include "buddy_hal/hal_imu.h"
#include "buddy_hal/hal.h"

#define TAG "IMU"

typedef struct {
    icm42670_handle_t handle;
} imu_priv_t;

static hal_imu_t  s_imu;
static imu_priv_t s_priv;

static esp_err_t imu_init(hal_imu_t *imu)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    icm42670_cfg_t cfg = {
        .acce_fs  = ACCE_FS_4G,
        .acce_odr = ACCE_ODR_400HZ,
        .gyro_fs  = GYRO_FS_500DPS,
        .gyro_odr = GYRO_ODR_400HZ,
    };
    return icm42670_config(p->handle, &cfg);
}

static esp_err_t imu_read_accel(hal_imu_t *imu, hal_accel_t *out)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    icm42670_value_t val;
    esp_err_t r = icm42670_get_acce_value(p->handle, &val);
    if (r == ESP_OK) { out->x = val.x; out->y = val.y; out->z = val.z; }
    return r;
}

static esp_err_t imu_read_gyro(hal_imu_t *imu, hal_gyro_t *out)
{
    imu_priv_t *p = (imu_priv_t *)imu->priv;
    icm42670_value_t val;
    esp_err_t r = icm42670_get_gyro_value(p->handle, &val);
    if (r == ESP_OK) { out->x = val.x; out->y = val.y; out->z = val.z; }
    return r;
}

static esp_err_t imu_read_both(hal_imu_t *imu, hal_accel_t *a, hal_gyro_t *g)
{
    imu_read_accel(imu, a);
    return imu_read_gyro(imu, g);
}

esp_err_t hal_imu_create(hal_imu_t **out)
{
    bsp_i2c_init();
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    icm42670_create(i2c_bus, ICM42670_I2C_ADDRESS, &s_priv.handle);

    s_imu.init       = imu_init;
    s_imu.read_accel = imu_read_accel;
    s_imu.read_gyro  = imu_read_gyro;
    s_imu.read_both  = imu_read_both;
    s_imu.priv       = &s_priv;

    *out = &s_imu;
    ESP_LOGI(TAG, "IMU created");
    return ESP_OK;
}

void hal_imu_destroy(hal_imu_t *imu) { }
