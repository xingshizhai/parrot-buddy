#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sound_detection.h"

esp_err_t parrot_core_init(void);
esp_err_t parrot_core_start(void);
bool      parrot_core_is_running(void);
