#include "loading_ui.h"

#include "esp_timer.h"

LV_FONT_DECLARE(lv_font_dejavu_32);

#define COLOR_BG          0x101418
#define COLOR_TEXT        0xE8E8E8
#define COLOR_RING_TRACK  0x2A2F36 // matches metronome_ui.c's palette
#define COLOR_ACCENT_BEAT 0xFFC857

#define LOADING_DURATION_US (5 * 1000 * 1000)
#define SPIN_ARC_SIZE     120
#define SPIN_ARC_WIDTH    10
#define SPIN_SWEEP_DEG    90   // how much of the ring is lit at once
#define SPIN_PERIOD_MS    1000 // one full revolution

static lv_obj_t *s_overlay;
static lv_obj_t *s_spinner;
static int64_t s_start_us;
static bool s_active;

static void spinner_rotate_anim_cb(void *var, int32_t value)
{
    lv_arc_set_rotation((lv_obj_t *)var, value);
}

void LoadingUI_Create(lv_obj_t *parent)
{
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, "Musician Dial");
    lv_obj_set_style_text_font(title, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    s_spinner = lv_arc_create(s_overlay);
    lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(s_spinner, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_spinner, SPIN_ARC_SIZE, SPIN_ARC_SIZE);
    lv_obj_center(s_spinner);
    lv_arc_set_bg_angles(s_spinner, 0, 360);
    lv_arc_set_angles(s_spinner, 0, SPIN_SWEEP_DEG);
    lv_obj_set_style_arc_width(s_spinner, SPIN_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, SPIN_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(COLOR_RING_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(COLOR_ACCENT_BEAT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_spinner, false, LV_PART_INDICATOR);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_spinner);
    lv_anim_set_exec_cb(&anim, spinner_rotate_anim_cb);
    lv_anim_set_values(&anim, 0, 360);
    lv_anim_set_duration(&anim, SPIN_PERIOD_MS);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);

    s_start_us = esp_timer_get_time();
    s_active = true;
}

bool LoadingUI_IsActive(void)
{
    return s_active;
}

void LoadingUI_Tick(void)
{
    if (!s_active) return;
    if (esp_timer_get_time() - s_start_us < LOADING_DURATION_US) return;

    lv_anim_delete(s_spinner, NULL);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_active = false;
}
