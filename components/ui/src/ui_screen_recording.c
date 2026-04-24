#include "ui/ui_screen_recording.h"
#include "lvgl.h"
#include "esp_log.h"

#define TAG "UI_RECORDING"

static lv_obj_t *screen = NULL;
static lv_obj_t *level_bar = NULL;
static lv_obj_t *duration_label = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *level_label = NULL;
static lv_obj_t *net_indicator = NULL;
static recording_state_cb_t s_state_cb = NULL;
static recording_level_cb_t s_level_cb = NULL;

static void on_back_click(lv_event_t *e) {
    ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_SLIDE_RIGHT);
}

esp_err_t ui_screen_recording_create(lv_obj_t *parent) {
    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Parrot Recording");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    level_bar = lv_bar_create(screen);
    lv_obj_set_size(level_bar, 280, 30);
    lv_bar_set_range(level_bar, -60, 0);
    lv_bar_set_value(level_bar, -60, LV_ANIM_OFF);
    lv_obj_align(level_bar, LV_ALIGN_TOP_MID, 0, 70);

    level_label = lv_label_create(screen);
    lv_label_set_text(level_label, "-60 dB");
    lv_obj_set_style_text_color(level_label, lv_color_white(), 0);
    lv_obj_align_to(level_label, level_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    duration_label = lv_label_create(screen);
    lv_label_set_text(duration_label, "Duration: 0s");
    lv_obj_set_style_text_color(duration_label, lv_color_white(), 0);
    lv_obj_align(duration_label, LV_ALIGN_TOP_MID, 0, 130);

    state_label = lv_label_create(screen);
    lv_label_set_text(state_label, "Status: Idle");
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x808080), 0);
    lv_obj_align(state_label, LV_ALIGN_TOP_MID, 0, 160);

    net_indicator = lv_obj_create(screen);
    lv_obj_set_size(net_indicator, 12, 12);
    lv_obj_set_style_bg_color(net_indicator, lv_color_hex(0x808080), 0);
    lv_obj_set_style_radius(net_indicator, 6, 0);
    lv_obj_align(net_indicator, LV_ALIGN_TOP_RIGHT, -20, 20);

    lv_obj_t *net_label = lv_label_create(screen);
    lv_label_set_text(net_label, "Net");
    lv_obj_set_style_text_color(net_label, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(net_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(net_label, net_indicator, LV_ALIGN_OUT_LEFT_MID, -5, 0);

    lv_obj_t *back_btn = lv_btn_create(screen);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    return ESP_OK;
}

esp_err_t ui_screen_recording_show(void) {
    if (!screen) {
        return ESP_FAIL;
    }

    lv_scr_load(screen);
    return ESP_OK;
}

esp_err_t ui_screen_recording_hide(void) {
    return ESP_OK;
}

esp_err_t ui_screen_recording_set_callbacks(recording_state_cb_t state_cb, recording_level_cb_t level_cb) {
    s_state_cb = state_cb;
    s_level_cb = level_cb;
    return ESP_OK;
}

esp_err_t ui_screen_recording_update_level(float level_db) {
    if (!level_bar || !level_label) {
        return ESP_FAIL;
    }

    if (level_db < -60) level_db = -60;
    if (level_db > 0) level_db = 0;

    lv_bar_set_value(level_bar, (int32_t)level_db, LV_ANIM_ON);

    char text[32];
    snprintf(text, sizeof(text), "%.1f dB", level_db);
    lv_label_set_text(level_label, text);

    if (level_db > -20) {
        lv_obj_set_style_bg_color(level_bar, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    } else if (level_db > -40) {
        lv_obj_set_style_bg_color(level_bar, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(level_bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    }

    return ESP_OK;
}

esp_err_t ui_screen_recording_update_duration(uint32_t duration_ms) {
    if (!duration_label) {
        return ESP_FAIL;
    }

    char text[32];
    snprintf(text, sizeof(text), "Duration: %ds", (int)(duration_ms / 1000));
    lv_label_set_text(duration_label, text);

    return ESP_OK;
}

esp_err_t ui_screen_recording_update_state(bool is_recording, bool is_streaming) {
    if (!state_label) {
        return ESP_FAIL;
    }

    char text[32];
    if (is_recording) {
        if (is_streaming) {
            snprintf(text, sizeof(text), "Status: Recording & Streaming");
            lv_obj_set_style_text_color(state_label, lv_palette_main(LV_PALETTE_GREEN), 0);
        } else {
            snprintf(text, sizeof(text), "Status: Recording (No Stream)");
            lv_obj_set_style_text_color(state_label, lv_palette_main(LV_PALETTE_YELLOW), 0);
        }
    } else {
        snprintf(text, sizeof(text), "Status: Idle");
        lv_obj_set_style_text_color(state_label, lv_color_hex(0x808080), 0);
    }

    lv_label_set_text(state_label, text);
    return ESP_OK;
}

esp_err_t ui_screen_recording_set_connected(bool connected) {
    if (!net_indicator) {
        return ESP_FAIL;
    }
    if (connected) {
        lv_obj_set_style_bg_color(net_indicator, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_obj_set_style_bg_color(net_indicator, lv_color_hex(0x808080), 0);
    }
    return ESP_OK;
}
