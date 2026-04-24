#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "audio_manager.h"
#include "ui/ui_manager.h"

#define TAG "UI_DBG"

/* ── Layout constants (320 × 240) ─────────────────────────────────────── */
#define SCR_W       320
#define SCR_H       240
#define TITLEBAR_H   30
#define VU_H         20
#define CHART_H      76
#define INFO_H       24
/* BUTTON_H = 240 - 30 - 20 - 76 - 24 = 90 */

#define WAVE_POINTS  80   /* rolling chart history length                   */
#define TONE_HZ      440
#define TONE_SAMPLES 8000 /* 0.5 s at 16 kHz                               */

/* ── Shared state (written by audio callbacks, read by LVGL timer) ─────── */
static volatile int32_t  s_peak_new    = 0;   /* latest chunk peak (0-32767) */
static volatile bool     s_has_chunk   = false;
static volatile size_t   s_rec_samples = 0;
static volatile bool     s_rec_active  = false;

/* Last complete recording: filled by record_done_cb */
static int16_t  *s_last_rec    = NULL;
static size_t    s_last_rec_n  = 0;
static uint32_t  s_last_sr     = 16000;

/* ── LVGL widget handles ────────────────────────────────────────────────── */
static lv_obj_t         *s_scr         = NULL;
static lv_obj_t         *s_lbl_status  = NULL;
static lv_obj_t         *s_bar_vu      = NULL;
static lv_obj_t         *s_chart       = NULL;
static lv_chart_series_t *s_chart_ser  = NULL;
static lv_obj_t         *s_lbl_info    = NULL;
static lv_obj_t         *s_btn_rec     = NULL;
static lv_obj_t         *s_lbl_rec     = NULL;
static lv_timer_t       *s_update_timer = NULL;

/* ── Tone buffer ────────────────────────────────────────────────────────── */
static int16_t  *s_tone_buf = NULL;

/* ── Helper: peak amplitude of a PCM chunk ─────────────────────────────── */
static int32_t pcm_peak(const int16_t *pcm, size_t n)
{
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > peak) peak = v;
    }
    return peak;
}

/* ── Audio callbacks (run from record/audio tasks) ─────────────────────── */

static void on_audio_chunk(const int16_t *pcm, size_t n_samples, void *ctx)
{
    /* Cheap: just compute peak and flag for LVGL timer */
    s_peak_new    = pcm_peak(pcm, n_samples);
    s_rec_samples += n_samples;
    s_has_chunk   = true;
}

static void on_record_done(const int16_t *pcm, size_t n_samples,
                            uint32_t sample_rate, void *ctx)
{
    /* Keep a copy of the last recording for playback */
    if (s_last_rec) { free(s_last_rec); s_last_rec = NULL; }

    if (n_samples > 0 && pcm) {
        s_last_rec = malloc(n_samples * sizeof(int16_t));
        if (s_last_rec) {
            /* Find peak amplitude */
            int32_t peak = 0;
            for (size_t i = 0; i < n_samples; i++) {
                int32_t v = pcm[i] < 0 ? -pcm[i] : pcm[i];
                if (v > peak) peak = v;
            }
            /* Normalize to ~90 % of full scale; cap SW boost at 24 dB (×16) */
            float scale = (peak > 200) ? (29491.0f / (float)peak) : 1.0f;
            if (scale > 16.0f) scale = 16.0f;
            for (size_t i = 0; i < n_samples; i++) {
                int32_t v = (int32_t)((float)pcm[i] * scale);
                s_last_rec[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
            }
            s_last_rec_n = n_samples;
            s_last_sr    = sample_rate;
            ESP_LOGI(TAG, "recording normalized: peak=%ld scale=%.1f", (long)peak, (double)scale);
        }
    }
    s_rec_samples = 0;
    s_rec_active  = false;
    ESP_LOGI(TAG, "recording done: %u samples", (unsigned)n_samples);
}

/* ── LVGL periodic update (100 ms) ─────────────────────────────────────── */

static void update_timer_cb(lv_timer_t *t)
{
    if (!s_scr) return;

    /* Status dot + label */
    const char *status_txt = s_rec_active      ? "#ff4444 ● REC#"
                           : audio_manager_is_playing() ? "#44aaff ● PLAY#"
                           : "#888888 ○ IDLE#";
    lv_label_set_text(s_lbl_status, status_txt);

    /* VU bar */
    if (s_has_chunk) {
        lv_bar_set_value(s_bar_vu, (int32_t)s_peak_new, LV_ANIM_OFF);
        lv_chart_set_next_value(s_chart, s_chart_ser, (lv_value_precise_t)s_peak_new);
        s_has_chunk = false;
    } else if (!s_rec_active) {
        /* Decay VU bar when not recording */
        int32_t cur = lv_bar_get_value(s_bar_vu);
        if (cur > 0) lv_bar_set_value(s_bar_vu, cur * 7 / 8, LV_ANIM_OFF);
    }

    /* Info label */
    float dur_s     = (float)s_rec_samples / (float)s_last_sr;
    size_t free_kb  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    if (free_kb == 0) free_kb = esp_get_free_heap_size() / 1024;

    char info[80];
    if (s_rec_active) {
        snprintf(info, sizeof(info), "%.1f s  |  %u smp  |  %u KB free",
                 dur_s, (unsigned)s_rec_samples, (unsigned)free_kb);
    } else if (s_last_rec_n > 0) {
        float last_dur = (float)s_last_rec_n / (float)s_last_sr;
        snprintf(info, sizeof(info), "Last: %.1f s  |  %u smp  |  %u KB free",
                 last_dur, (unsigned)s_last_rec_n, (unsigned)free_kb);
    } else {
        snprintf(info, sizeof(info), "No recording  |  %u KB free",
                 (unsigned)free_kb);
    }
    lv_label_set_text(s_lbl_info, info);

    /* Record button color */
    lv_color_t rec_col = s_rec_active
                         ? lv_color_make(0xCC, 0x11, 0x11)
                         : lv_color_make(0x22, 0x55, 0xCC);
    lv_obj_set_style_bg_color(s_btn_rec, rec_col, 0);
    lv_label_set_text(s_lbl_rec, s_rec_active ? "STOP REC" : "RECORD");
}

/* ── Button callbacks ───────────────────────────────────────────────────── */

static void record_btn_cb(lv_event_t *e)
{
    if (s_rec_active) {
        audio_manager_record_stop();
        /* s_rec_active cleared by on_record_done callback */
    } else {
        s_rec_samples = 0;
        s_rec_active  = true;
        audio_manager_record_start();
    }
}

static void play_btn_cb(lv_event_t *e)
{
    if (!s_last_rec || s_last_rec_n == 0) {
        ESP_LOGW(TAG, "no recording to play");
        return;
    }
    if (audio_manager_is_playing()) {
        audio_manager_play_stop();
        return;
    }
    audio_manager_set_volume(100);
    esp_err_t r = audio_manager_play_raw(s_last_rec, s_last_rec_n);
    if (r != ESP_OK) ESP_LOGE(TAG, "play_raw failed: %s", esp_err_to_name(r));
}

static void tone_btn_cb(lv_event_t *e)
{
    if (audio_manager_is_playing()) {
        audio_manager_play_stop();
        return;
    }

    /* Lazy-allocate tone buffer */
    if (!s_tone_buf) {
        s_tone_buf = heap_caps_malloc(TONE_SAMPLES * sizeof(int16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_tone_buf) s_tone_buf = malloc(TONE_SAMPLES * sizeof(int16_t));
        if (!s_tone_buf) { ESP_LOGE(TAG, "tone buf alloc failed"); return; }

        for (int i = 0; i < TONE_SAMPLES; i++) {
            float t = (float)i / s_last_sr;
            s_tone_buf[i] = (int16_t)(20000.0f * sinf(2.0f * M_PI * TONE_HZ * t));
        }
    }

    audio_manager_set_volume(100);
    esp_err_t r = audio_manager_play_raw(s_tone_buf, TONE_SAMPLES);
    if (r != ESP_OK) ESP_LOGE(TAG, "tone play failed: %s", esp_err_to_name(r));
    else ESP_LOGI(TAG, "playing %d Hz test tone", TONE_HZ);
}

static void back_btn_cb(lv_event_t *e)
{
    /* Stop any ongoing activity before leaving */
    if (s_rec_active) {
        audio_manager_record_stop();
        s_rec_active = false;
    }
    if (audio_manager_is_playing()) audio_manager_play_stop();

    if (lvgl_port_lock(50)) {
        if (s_update_timer) { lv_timer_pause(s_update_timer); }
        lvgl_port_unlock();
    }
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

/* ── Public: create the screen ─────────────────────────────────────────── */

lv_obj_t *screen_debug_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x0A, 0x0A, 0x12), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    s_scr = scr;

    /* ── Title bar ─────────────────────────────────────────────────────── */
    lv_obj_t *titlebar = lv_obj_create(scr);
    lv_obj_set_size(titlebar, SCR_W, TITLEBAR_H);
    lv_obj_align(titlebar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(titlebar, lv_color_make(0x15, 0x15, 0x25), 0);
    lv_obj_set_style_border_width(titlebar, 0, 0);
    lv_obj_set_style_radius(titlebar, 0, 0);
    lv_obj_set_style_pad_all(titlebar, 4, 0);

    lv_obj_t *btn_back = lv_btn_create(titlebar);
    lv_obj_set_size(btn_back, 50, 22);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_back);

    lv_obj_t *lbl_title = lv_label_create(titlebar);
    lv_label_set_text(lbl_title, "Audio Debug");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    s_lbl_status = lv_label_create(titlebar);
    lv_label_set_text(s_lbl_status, "#888888 ○ IDLE#");
    lv_label_set_recolor(s_lbl_status, true);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_RIGHT_MID, -4, 0);

    /* ── VU bar ────────────────────────────────────────────────────────── */
    lv_obj_t *vu_row = lv_obj_create(scr);
    lv_obj_set_size(vu_row, SCR_W, VU_H + 4);
    lv_obj_align(vu_row, LV_ALIGN_TOP_LEFT, 0, TITLEBAR_H);
    lv_obj_set_style_bg_color(vu_row, lv_color_make(0x10, 0x10, 0x1A), 0);
    lv_obj_set_style_border_width(vu_row, 0, 0);
    lv_obj_set_style_radius(vu_row, 0, 0);
    lv_obj_set_style_pad_hor(vu_row, 8, 0);
    lv_obj_set_style_pad_ver(vu_row, 2, 0);

    lv_obj_t *vu_label = lv_label_create(vu_row);
    lv_label_set_text(vu_label, "VU");
    lv_obj_set_style_text_color(vu_label, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_align(vu_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_bar_vu = lv_bar_create(vu_row);
    lv_obj_set_size(s_bar_vu, SCR_W - 40, VU_H);
    lv_obj_align(s_bar_vu, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_bar_set_range(s_bar_vu, 0, 32767);
    lv_bar_set_value(s_bar_vu, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_vu, lv_color_make(0x20, 0x20, 0x30), 0);
    lv_obj_set_style_bg_color(s_bar_vu, lv_color_make(0x00, 0xCC, 0x44),
                               LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_vu, 4, 0);
    lv_obj_set_style_radius(s_bar_vu, 4, LV_PART_INDICATOR);

    /* ── Waveform chart ────────────────────────────────────────────────── */
    s_chart = lv_chart_create(scr);
    lv_obj_set_size(s_chart, SCR_W, CHART_H);
    lv_obj_align(s_chart, LV_ALIGN_TOP_LEFT, 0, TITLEBAR_H + VU_H + 4);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, WAVE_POINTS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 32767);
    lv_chart_set_div_line_count(s_chart, 3, 0);
    lv_obj_set_style_bg_color(s_chart, lv_color_make(0x05, 0x05, 0x10), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_pad_all(s_chart, 4, 0);

    s_chart_ser = lv_chart_add_series(s_chart,
                                       lv_color_make(0x00, 0xDD, 0x66),
                                       LV_CHART_AXIS_PRIMARY_Y);
    /* Pre-fill with zeros */
    for (int i = 0; i < WAVE_POINTS; i++) {
        lv_chart_set_next_value(s_chart, s_chart_ser, 0);
    }

    /* ── Info row ──────────────────────────────────────────────────────── */
    lv_obj_t *info_row = lv_obj_create(scr);
    lv_obj_set_size(info_row, SCR_W, INFO_H);
    lv_obj_align(info_row, LV_ALIGN_TOP_LEFT, 0, TITLEBAR_H + VU_H + 4 + CHART_H);
    lv_obj_set_style_bg_color(info_row, lv_color_make(0x10, 0x10, 0x1A), 0);
    lv_obj_set_style_border_width(info_row, 0, 0);
    lv_obj_set_style_radius(info_row, 0, 0);
    lv_obj_set_style_pad_all(info_row, 4, 0);

    s_lbl_info = lv_label_create(info_row);
    lv_label_set_text(s_lbl_info, "No recording");
    lv_obj_set_style_text_color(s_lbl_info, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_align(s_lbl_info, LV_ALIGN_LEFT_MID, 0, 0);

    /* ── Button row ────────────────────────────────────────────────────── */
    int btn_y = TITLEBAR_H + VU_H + 4 + CHART_H + INFO_H;
    int btn_h = SCR_H - btn_y;
    int btn_w = SCR_W / 3 - 4;

    /* RECORD button */
    s_btn_rec = lv_btn_create(scr);
    lv_obj_set_size(s_btn_rec, btn_w, btn_h - 8);
    lv_obj_align(s_btn_rec, LV_ALIGN_TOP_LEFT, 4, btn_y + 4);
    lv_obj_set_style_bg_color(s_btn_rec, lv_color_make(0x22, 0x55, 0xCC), 0);
    lv_obj_add_event_cb(s_btn_rec, record_btn_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_rec = lv_label_create(s_btn_rec);
    lv_label_set_text(s_lbl_rec, "RECORD");
    lv_obj_set_style_text_font(s_lbl_rec, &lv_font_montserrat_16, 0);
    lv_obj_center(s_lbl_rec);

    /* PLAY button */
    lv_obj_t *btn_play = lv_btn_create(scr);
    lv_obj_set_size(btn_play, btn_w, btn_h - 8);
    lv_obj_align(btn_play, LV_ALIGN_TOP_MID, 0, btn_y + 4);
    lv_obj_set_style_bg_color(btn_play, lv_color_make(0x22, 0x88, 0x44), 0);
    lv_obj_add_event_cb(btn_play, play_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_play = lv_label_create(btn_play);
    lv_label_set_text(lbl_play, LV_SYMBOL_PLAY " PLAY");
    lv_obj_set_style_text_font(lbl_play, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_play);

    /* TONE button */
    lv_obj_t *btn_tone = lv_btn_create(scr);
    lv_obj_set_size(btn_tone, btn_w, btn_h - 8);
    lv_obj_align(btn_tone, LV_ALIGN_TOP_RIGHT, -4, btn_y + 4);
    lv_obj_set_style_bg_color(btn_tone, lv_color_make(0x77, 0x44, 0x00), 0);
    lv_obj_add_event_cb(btn_tone, tone_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_tone = lv_label_create(btn_tone);
    lv_label_set_text(lbl_tone, LV_SYMBOL_AUDIO " 440Hz");
    lv_obj_set_style_text_font(lbl_tone, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_tone);

    /* ── Periodic update timer ─────────────────────────────────────────── */
    s_update_timer = lv_timer_create(update_timer_cb, 100, NULL);
    lv_timer_pause(s_update_timer); /* start paused; resume on show */

    /* ── Register audio callbacks ──────────────────────────────────────── */
    audio_manager_set_chunk_cb(on_audio_chunk, NULL);
    audio_manager_set_record_done_cb(on_record_done, NULL);

    ESP_LOGI(TAG, "debug screen created");
    return scr;
}

/* Called by ui_manager when this screen becomes active */
void ui_screen_debug_on_show(void)
{
    s_rec_samples = 0;
    s_has_chunk   = false;
    if (lvgl_port_lock(50)) {
        if (s_update_timer) lv_timer_resume(s_update_timer);
        lvgl_port_unlock();
    }
}

/* Called by ui_manager when leaving this screen */
void ui_screen_debug_on_hide(void)
{
    if (lvgl_port_lock(50)) {
        if (s_update_timer) lv_timer_pause(s_update_timer);
        lvgl_port_unlock();
    }
    /* Remove audio callbacks so they don't fire when screen is invisible */
    audio_manager_set_chunk_cb(NULL, NULL);
    audio_manager_set_record_done_cb(NULL, NULL);
}
