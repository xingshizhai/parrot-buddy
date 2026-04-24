#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "buddy_hal/hal_storage.h"

#define TAG       "STORAGE"
#define NVS_NS    "sb"

static nvs_handle_t s_nvs = 0;

esp_err_t hal_storage_init(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);
    return nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
}

esp_err_t hal_storage_deinit(void)
{
    if (s_nvs) { nvs_close(s_nvs); s_nvs = 0; }
    return ESP_OK;
}

esp_err_t hal_storage_set_u32(const char *key, uint32_t val)
{
    esp_err_t r = nvs_set_u32(s_nvs, key, val);
    if (r == ESP_OK) nvs_commit(s_nvs);
    return r;
}

esp_err_t hal_storage_get_u32(const char *key, uint32_t *val, uint32_t def)
{
    esp_err_t r = nvs_get_u32(s_nvs, key, val);
    if (r != ESP_OK) *val = def;
    return ESP_OK;
}

esp_err_t hal_storage_set_str(const char *key, const char *val)
{
    esp_err_t r = nvs_set_str(s_nvs, key, val);
    if (r == ESP_OK) nvs_commit(s_nvs);
    return r;
}

esp_err_t hal_storage_get_str(const char *key, char *buf, size_t len)
{
    return nvs_get_str(s_nvs, key, buf, &len);
}

esp_err_t hal_storage_set_blob(const char *key, const void *data, size_t len)
{
    esp_err_t r = nvs_set_blob(s_nvs, key, data, len);
    if (r == ESP_OK) nvs_commit(s_nvs);
    return r;
}

esp_err_t hal_storage_get_blob(const char *key, void *buf, size_t *len)
{
    return nvs_get_blob(s_nvs, key, buf, len);
}

esp_err_t hal_storage_erase_key(const char *key)
{
    esp_err_t r = nvs_erase_key(s_nvs, key);
    if (r == ESP_OK) nvs_commit(s_nvs);
    return r;
}

esp_err_t hal_storage_erase_all(void)
{
    esp_err_t r = nvs_erase_all(s_nvs);
    if (r == ESP_OK) nvs_commit(s_nvs);
    return r;
}
