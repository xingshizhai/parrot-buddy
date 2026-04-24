#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "buddy_hal/hal.h"

#define TAG "UI"

#ifndef CONFIG_UI_SCREEN_OFF_TIMEOUT_S
#define CONFIG_UI_SCREEN_OFF_TIMEOUT_S 30
#endif
#define SCREEN_STACK_DEPTH 4

static ui_screen_id_t s_stack[SCREEN_STACK_DEPTH];
static int            s_stack_top = -1;
static lv_obj_t      *s_screens[UI_SCREEN_MAX] = {0};

/* Forward declarations */
static lv_obj_t *screen_boot_create(void);
static lv_obj_t *screen_main_create(void);
static lv_obj_t *screen_recording_create_wrapper(void);
static lv_obj_t *screen_settings_create(void);
static lv_obj_t *screen_status_create(void);

/* Defined in ui_screen_debug.c */
lv_obj_t *screen_debug_create(void);
void      ui_screen_debug_on_show(void);
void      ui_screen_debug_on_hide(void);

/* Main screen status label */
static lv_obj_t   *s_main_status_label = NULL;

/* Status screen heap label */
static lv_obj_t   *s_status_heap = NULL;

static lv_timer_t *s_screenoff_timer = NULL;

/* ── Screen-off timer ──────────────────────────────────────────────────── */

static void screenoff_timer_cb(lv_timer_t *t)
{
    extern hal_handles_t g_hal;
    if (g_hal.display)
        g_hal.display->backlight_set(g_hal.display, 0);
}

static void reset_screenoff_timer(void)
{
#if CONFIG_UI_SCREEN_OFF_TIMEOUT_S > 0
    if (!s_screenoff_timer) return;
    extern hal_handles_t g_hal;
    if (g_hal.display)
        g_hal.display->backlight_set(g_hal.display, 100);
    lv_timer_reset(s_screenoff_timer);
    lv_timer_resume(s_screenoff_timer);
#endif
}

/* ── Boot screen ───────────────────────────────────────────────────────── */

static lv_obj_t *screen_boot_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Parrot Buddy\nStarting...");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return scr;
}

/* ── Main screen ───────────────────────────────────────────────────────── */

static void main_settings_btn_cb(lv_event_t *e)
{
    reset_screenoff_timer();
    ui_manager_push(UI_SCREEN_SETTINGS, UI_ANIM_SLIDE_LEFT);
}

static void main_recording_btn_cb(lv_event_t *e)
{
    reset_screenoff_timer();
    ui_manager_push(UI_SCREEN_RECORDING, UI_ANIM_SLIDE_LEFT);
}

static void main_touch_cb(lv_event_t *e)
{
    reset_screenoff_timer();
}

/* ── Recording screen ──────────────────────────────────────────────────── */

#include "ui/ui_screen_recording.h"

static lv_obj_t *screen_recording_create_wrapper(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_screen_recording_create(scr);
    return scr;
}

static lv_obj_t *screen_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_add_event_cb(scr, main_touch_cb, LV_EVENT_CLICKED, NULL);

    /* Title bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, 28);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x20, 0x20, 0x20), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* App title in status bar */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Parrot Buddy");
    lv_obj_set_style_text_color(title, lv_color_make(0xAA, 0xAA, 0xAA), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* Settings gear button */
    lv_obj_t *btn_cfg = lv_btn_create(scr);
    lv_obj_set_size(btn_cfg, 36, 28);
    lv_obj_align(btn_cfg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cfg, lv_color_make(0x38, 0x38, 0x38), 0);
    lv_obj_set_style_bg_color(btn_cfg, lv_color_make(0x60, 0x60, 0x60), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_cfg, 0, 0);
    lv_obj_set_style_shadow_width(btn_cfg, 0, 0);
    lv_obj_set_style_pad_all(btn_cfg, 0, 0);
    lv_obj_add_event_cb(btn_cfg, main_settings_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *icon = lv_label_create(btn_cfg);
    lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(icon, lv_color_make(0xDD, 0xDD, 0xDD), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_center(icon);

    /* Parrot avatar */
    lv_obj_t *avatar = lv_label_create(scr);
    lv_label_set_text(avatar, "(` v `)");
    lv_obj_set_style_text_color(avatar, lv_color_make(0x00, 0xCC, 0x55), 0);
    lv_obj_set_style_text_font(avatar, &lv_font_montserrat_24, 0);
    lv_obj_align(avatar, LV_ALIGN_CENTER, 0, -20);

    /* Status label */
    s_main_status_label = lv_label_create(scr);
    lv_label_set_text(s_main_status_label, "Listening...");
    lv_obj_set_style_text_color(s_main_status_label, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_set_style_text_font(s_main_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_main_status_label, LV_ALIGN_CENTER, 0, 30);

    /* Parrot Recording button */
    lv_obj_t *btn_rec = lv_btn_create(scr);
    lv_obj_set_size(btn_rec, 200, 44);
    lv_obj_set_style_bg_color(btn_rec, lv_color_make(0x20, 0x60, 0x30), 0);
    lv_obj_set_style_radius(btn_rec, 8, 0);
    lv_obj_align(btn_rec, LV_ALIGN_CENTER, 0, 90);
    lv_obj_add_event_cb(btn_rec, main_recording_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_rec = lv_label_create(btn_rec);
    lv_label_set_text(lbl_rec, "Parrot Recording");
    lv_obj_set_style_text_color(lbl_rec, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_rec, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_rec);

    return scr;
}

/* ── Status screen ─────────────────────────────────────────────────────── */

static void status_back_cb(lv_event_t *e) { ui_manager_pop(UI_ANIM_SLIDE_RIGHT); }

static lv_obj_t *make_stat_row(lv_obj_t *parent, int y, const char *icon, const char *init)
{
    lv_obj_t *icon_lbl = lv_label_create(parent);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_make(0x88, 0x88, 0x88), 0);
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT, 10, y);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, init);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 36, y);
    return val;
}

static lv_obj_t *screen_status_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x08, 0x08, 0x10), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x15, 0x15, 0x25), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_bk = lv_btn_create(bar);
    lv_obj_set_size(btn_bk, 50, 26);
    lv_obj_align(btn_bk, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_bk, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_add_event_cb(btn_bk, status_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bk = lv_label_create(btn_bk);
    lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_bk);

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "Status");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    s_status_heap = make_stat_row(scr, 44, LV_SYMBOL_SETTINGS, "-- KB free");

    return scr;
}

/* ── Settings screen ───────────────────────────────────────────────────── */

static void settings_debug_btn_cb(lv_event_t *e)
{
    ui_manager_push(UI_SCREEN_DEBUG, UI_ANIM_SLIDE_LEFT);
}

static void settings_status_btn_cb(lv_event_t *e)
{
    ui_screen_status_refresh();
    ui_manager_push(UI_SCREEN_STATUS, UI_ANIM_SLIDE_LEFT);
}

static void settings_back_btn_cb(lv_event_t *e)
{
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

static lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x0A, 0x0A, 0x12), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x15, 0x15, 0x25), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);

    lv_obj_t *btn_bk = lv_btn_create(bar);
    lv_obj_set_size(btn_bk, 50, 26);
    lv_obj_align(btn_bk, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_bk, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_add_event_cb(btn_bk, settings_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bk = lv_label_create(btn_bk);
    lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_bk);

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 320, 240 - 36);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_bg_color(list, lv_color_make(0x0A, 0x0A, 0x12), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 2, 0);

    lv_obj_t *btn_st = lv_list_add_btn(list, LV_SYMBOL_LIST, "Device Status");
    lv_obj_set_style_bg_color(btn_st, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn_st, lv_color_white(), 0);
    lv_obj_add_event_cb(btn_st, settings_status_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_AUDIO, "Audio Debug");
    lv_obj_set_style_bg_color(btn, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_add_event_cb(btn, settings_debug_btn_cb, LV_EVENT_CLICKED, NULL);

#ifdef CONFIG_PARROT_RECORDING_ENABLE
    lv_obj_t *sep = lv_obj_create(list);
    lv_obj_set_size(sep, 300, 1);
    lv_obj_set_style_bg_color(sep, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    char buf[64];
    lv_obj_t *btn_silence = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "Silence Timeout");
    lv_obj_set_style_bg_color(btn_silence, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn_silence, lv_color_white(), 0);

    snprintf(buf, sizeof(buf), "Silence Timeout: %d s", CONFIG_PARROT_RECORDING_SILENCE_TIMEOUT_MS / 1000);
    lv_obj_t *lbl_silence = lv_label_create(list);
    lv_label_set_text(lbl_silence, buf);
    lv_obj_set_style_text_color(lbl_silence, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_set_style_text_font(lbl_silence, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_silence, LV_ALIGN_TOP_LEFT, 40, 0);

    snprintf(buf, sizeof(buf), "Min Duration: %d s", CONFIG_PARROT_RECORDING_MIN_DURATION_MS / 1000);
    lv_obj_t *btn_dur = lv_list_add_btn(list, LV_SYMBOL_PLAY, "Min Duration");
    lv_obj_set_style_bg_color(btn_dur, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn_dur, lv_color_white(), 0);

    snprintf(buf, sizeof(buf), "Threshold: %d dB", CONFIG_PARROT_RECORDING_DETECTION_THRESHOLD_DB);
    lv_obj_t *btn_thr = lv_list_add_btn(list, LV_SYMBOL_POWER, "Detection Threshold");
    lv_obj_set_style_bg_color(btn_thr, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn_thr, lv_color_white(), 0);
#else
    (void)buf;
#endif

    return scr;
}

/* ── Screen factory ────────────────────────────────────────────────────── */

static lv_obj_t *create_screen(ui_screen_id_t id)
{
    switch (id) {
    case UI_SCREEN_BOOT:      return screen_boot_create();
    case UI_SCREEN_MAIN:      return screen_main_create();
    case UI_SCREEN_RECORDING: return screen_recording_create_wrapper();
    case UI_SCREEN_SETTINGS:  return screen_settings_create();
    case UI_SCREEN_STATUS:    return screen_status_create();
    case UI_SCREEN_DEBUG:     return screen_debug_create();
    default:                  return NULL;
    }
}

static void notify_screen_lifecycle(ui_screen_id_t leaving, ui_screen_id_t entering)
{
    if (leaving == UI_SCREEN_DEBUG)  ui_screen_debug_on_hide();
    if (entering == UI_SCREEN_DEBUG) ui_screen_debug_on_show();
}

/* ── Public API ────────────────────────────────────────────────────────── */

esp_err_t ui_manager_init(void)
{
    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        s_screens[i] = create_screen((ui_screen_id_t)i);
    }
#if CONFIG_UI_SCREEN_OFF_TIMEOUT_S > 0
    s_screenoff_timer = lv_timer_create(screenoff_timer_cb,
                                         CONFIG_UI_SCREEN_OFF_TIMEOUT_S * 1000,
                                         NULL);
    lv_timer_set_repeat_count(s_screenoff_timer, 1);
#endif
    return ESP_OK;
}

void ui_manager_deinit(void)
{
    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        if (s_screens[i]) lv_obj_del(s_screens[i]);
    }
}

esp_err_t ui_manager_show(ui_screen_id_t id, ui_anim_type_t anim)
{
    if (id >= UI_SCREEN_MAX) return ESP_ERR_INVALID_ARG;
    ui_screen_id_t prev = ui_manager_current();
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[id],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON     :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT  :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        s_stack_top = 0;
        s_stack[0]  = id;
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, id);
    reset_screenoff_timer();
    return ESP_OK;
}

esp_err_t ui_manager_push(ui_screen_id_t id, ui_anim_type_t anim)
{
    ui_screen_id_t prev = ui_manager_current();
    if (s_stack_top < SCREEN_STACK_DEPTH - 1) {
        s_stack[++s_stack_top] = id;
    }
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[id],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON     :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT  :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, id);
    return ESP_OK;
}

esp_err_t ui_manager_pop(ui_anim_type_t anim)
{
    ui_screen_id_t prev = ui_manager_current();
    if (s_stack_top > 0) s_stack_top--;
    ui_screen_id_t next = s_stack[s_stack_top];
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[next],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON     :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT  :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, next);
    return ESP_OK;
}

ui_screen_id_t ui_manager_current(void)
{
    return s_stack_top >= 0 ? s_stack[s_stack_top] : UI_SCREEN_BOOT;
}

void ui_screen_main_set_status(const char *text)
{
    if (lvgl_port_lock(100)) {
        if (s_main_status_label)
            lv_label_set_text(s_main_status_label, text ? text : "");
        lvgl_port_unlock();
    }
}

void ui_screen_status_refresh(void)
{
    uint32_t free_kb = esp_get_free_heap_size() / 1024;
    if (!lvgl_port_lock(100)) return;
    if (s_status_heap) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu KB free", (unsigned long)free_kb);
        lv_label_set_text(s_status_heap, buf);
    }
    lvgl_port_unlock();
}
