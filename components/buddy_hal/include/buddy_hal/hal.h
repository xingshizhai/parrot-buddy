#pragma once

#include "hal_display.h"
#include "hal_touch.h"
#include "hal_audio.h"
#include "hal_button.h"
#include "hal_imu.h"
#include "hal_led.h"
#include "hal_storage.h"

typedef struct {
    hal_display_t  *display;
    hal_touch_t    *touch;
    hal_audio_t    *audio;
    hal_buttons_t  *buttons;
    hal_imu_t      *imu;
    hal_led_t      *led;
} hal_handles_t;

extern hal_handles_t g_hal;
