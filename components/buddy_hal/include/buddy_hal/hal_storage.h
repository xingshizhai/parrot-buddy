#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t hal_storage_init(void);
esp_err_t hal_storage_deinit(void);

esp_err_t hal_storage_set_u32(const char *key, uint32_t val);
esp_err_t hal_storage_get_u32(const char *key, uint32_t *val, uint32_t def);

esp_err_t hal_storage_set_str(const char *key, const char *val);
esp_err_t hal_storage_get_str(const char *key, char *buf, size_t len);

esp_err_t hal_storage_set_blob(const char *key, const void *data, size_t len);
esp_err_t hal_storage_get_blob(const char *key, void *buf, size_t *len);

esp_err_t hal_storage_erase_key(const char *key);
esp_err_t hal_storage_erase_all(void);
