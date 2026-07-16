#include "metronome_ui.h"
#include "metronome_engine.h"
#include "LVGL_Driver.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// DejaVu Sans (bundled with LVGL's own repo, lv_font_conv'd at these sizes)
// instead of Montserrat for regular-weight text -- see the same note in
// circle_of_fifths_ui.c: Montserrat's "G" is too easily mistaken for a "C"
// at small sizes on this display, DejaVu's spur is much more open.
LV_FONT_DECLARE(lv_font_dejavu_18);
LV_FONT_DECLARE(lv_font_dejavu_32);
LV_FONT_DECLARE(lv_font_dejavu_48);
LV_FONT_DECLARE(lv_font_tabler_icons_32);

// Tabler Icons' "circle-dashed" (U+ED27) and "metronome" (U+FD25) glyphs,
// pulled into their own small font (main/fonts/lv_font_tabler_icons_32.c) --
// same merge-a-glyph-font-in approach this project's other fonts already
// use for FontAwesome icons, just as a standalone 2-glyph font here.
#define ICON_CIRCLE_DASHED "\xEE\xB4\xA7"
#define ICON_METRONOME     "\xEF\xB4\xA5"

/* Screen is 480x480, center at (240,240). Interactive/text content is kept
 * within roughly a 190px radius of center so nothing sits under the round
 * bezel; the pulse ring is allowed to run out near the true edge. Nudge the
 * Y offsets below if your bezel crops a different amount once flashed. */
#define SCREEN_CENTER_X   240
#define SCREEN_CENTER_Y   240

// Named so the numpad-dialog trigger below can hit-test against the same
// boxes these buttons are actually created at, instead of duplicating the
// literal coordinates and risking drift.
#define BPM_BTN_SIZE  74
#define MINUS_BTN_X   (SCREEN_CENTER_X - 139)
#define MINUS_BTN_Y   113
#define PLUS_BTN_X    (SCREEN_CENTER_X + 65)
#define PLUS_BTN_Y    113

// The value+unit stack sits in the gap between the two buttons above: from
// the minus button's right edge (MINUS_BTN_X + BPM_BTN_SIZE) to the plus
// button's left edge (PLUS_BTN_X), which is symmetric around
// SCREEN_CENTER_X, so simple horizontal centering lands it correctly.
#define BPM_STACK_WIDTH  (PLUS_BTN_X - (MINUS_BTN_X + BPM_BTN_SIZE))
#define BPM_STACK_Y      108

#define RING_DIAMETER     456
#define RING_WIDTH        24 // 50% thicker than the original 16px

#define NEEDLE_WIDTH      8
#define NEEDLE_HEIGHT     190
// 0.1-degree units. 450 = 45.0 degrees each way, a 90-degree total swing
// (1/4 of the full circle around the pivot). Widened from the original 300
// (30 degrees each way); at this angle the needle's own body can sweep
// over the BPM stack and +/- buttons at the extremes of its travel -- see
// the needle/marker creation order note below for why that's fine.
#define NEEDLE_MAX_ANGLE  450

// Small radial tick marks near the display's physical edge, showing where
// the needle's swing starts and ends. Same center-pivot rotation trick as
// the needle itself: each tick is drawn vertical at rotation 0, then
// rotated by the same +-NEEDLE_MAX_ANGLE the needle swings to, so it ends
// up aligned along the radius at that angle.
#define MARKER_WIDTH      6
#define MARKER_LENGTH     30
#define MARKER_RADIUS     230 // near the true edge, same territory as the beat ring

#define COLOR_BG           0x101418
#define COLOR_RING_TRACK   0x2A2F36
#define COLOR_ACCENT_BEAT  0xFFC857 // downbeat
#define COLOR_ACCENT_OTHER 0x4FD1C5 // regular beat
#define COLOR_NEEDLE        0xFFC857 // gold, matches COLOR_ACCENT_BEAT
#define COLOR_MARKER         0x2F80ED // blue endpoint markers
#define COLOR_TEXT           0xE8E8E8
#define COLOR_TEXT_DIM       0x6B7280 // unselected mode label
#define COLOR_BTN            0x1F252C
#define COLOR_BTN_ACTIVE     0x3A4552
#define COLOR_BTN_START      0x2E7D32
#define COLOR_BTN_STOP       0xB3261E

typedef enum {
    DISPLAY_PENDULUM,
    DISPLAY_RING,
} display_mode_t;

static display_mode_t s_mode = DISPLAY_RING;
static bool s_needle_going_right = false; // flipped so beat 1's swing goes right, not left

static lv_obj_t *s_screen;
static lv_obj_t *s_ring;
static lv_obj_t *s_needle;
static lv_obj_t *s_marker_left;
static lv_obj_t *s_marker_right;
static lv_obj_t *s_bpm_value_label;
static lv_obj_t *s_start_stop_btn;
static lv_obj_t *s_start_stop_label;
static lv_obj_t *s_mode_switch;
static lv_obj_t *s_mode_label_ring;
static lv_obj_t *s_mode_label_pendulum;

// Long-press the BPM readout to open a numeric-entry dialog instead of
// stepping one BPM at a time.
static bool s_numpad_open = false;
static char s_numpad_entry[4] = "";
static lv_obj_t *s_numpad_overlay = NULL;
static lv_obj_t *s_numpad_entry_label = NULL;

// Long-press Start/Stop to open a time-signature picker.
static bool s_timesig_open = false;
static lv_obj_t *s_timesig_overlay = NULL;

static void refresh_bpm_label(void)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", Metronome_GetBPM());
    lv_label_set_text(s_bpm_value_label, buf);
}

static void apply_mode_visibility(void)
{
    bool show_needle = (s_mode == DISPLAY_PENDULUM);
    bool show_ring = (s_mode == DISPLAY_RING);

    if (show_needle) {
        lv_obj_remove_flag(s_needle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_marker_left, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_marker_right, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_needle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_marker_left, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_marker_right, LV_OBJ_FLAG_HIDDEN);
    }
    if (show_ring) {
        lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    }

    // Switch position itself (knob left = Ring, right = Pendulum) is the
    // primary selected/not-selected signal -- unambiguous regardless of any
    // theme bevel/shadow, unlike the two-separate-buttons version this
    // replaced. Dimming the inactive side's label reinforces it further.
    bool pendulum_selected = (s_mode == DISPLAY_PENDULUM);
    if (pendulum_selected) {
        lv_obj_add_state(s_mode_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_mode_switch, LV_STATE_CHECKED);
    }
    lv_obj_set_style_text_color(s_mode_label_ring,
                                 lv_color_hex(pendulum_selected ? COLOR_TEXT_DIM : COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_mode_label_pendulum,
                                 lv_color_hex(pendulum_selected ? COLOR_TEXT : COLOR_TEXT_DIM), 0);
}

static void mode_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    s_mode = lv_obj_has_state(sw, LV_STATE_CHECKED) ? DISPLAY_PENDULUM : DISPLAY_RING;
    apply_mode_visibility();
}

static void reset_visuals(void)
{
    lv_anim_delete(s_needle, NULL);
    lv_obj_set_style_transform_rotation(s_needle, 0, 0);
    s_needle_going_right = false;

    lv_arc_set_value(s_ring, 0);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(COLOR_ACCENT_OTHER), LV_PART_INDICATOR);
}

static void start_stop_event_cb(lv_event_t *e)
{
    if (Metronome_IsRunning()) {
        Metronome_Stop();
        lv_label_set_text(s_start_stop_label, LV_SYMBOL_PLAY " START");
        lv_obj_set_style_bg_color(s_start_stop_btn, lv_color_hex(COLOR_BTN_START), 0);
        reset_visuals();
    } else {
        Metronome_Start();
        lv_label_set_text(s_start_stop_label, LV_SYMBOL_STOP " STOP");
        lv_obj_set_style_bg_color(s_start_stop_btn, lv_color_hex(COLOR_BTN_STOP), 0);
    }
}

static void tap_event_cb(lv_event_t *e)
{
    Metronome_TapTempo();
    refresh_bpm_label();
}

static void bpm_step_event_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    int bpm = (int)Metronome_GetBPM() + step;
    Metronome_SetBPM((uint16_t)bpm);
    refresh_bpm_label();
}

static lv_obj_t *make_button_ex(lv_obj_t *parent, const char *text, int w, int h, int x, int y,
                                 lv_event_code_t code, lv_event_cb_t cb, void *user_data, const lv_font_t *font)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BTN), 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    // Without this, a swipe that starts on a button never reaches the
    // screen-level gesture handler: LVGL only sends LV_EVENT_GESTURE to the
    // exact widget the touch began on, bubbling to parents only if they
    // opt in with this flag.
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // Without this, natural finger tremor during a long-press hold can
    // drift the touch point off this button and back (even within the same
    // physical button), which makes LVGL's indev re-detect "the object
    // under the point" and reset its long-press-sent tracking -- so a
    // genuinely-long press can still end up firing SHORT_CLICKED on
    // release, right after LONG_PRESSED already fired. PRESS_LOCK keeps
    // this button "the pressed one" for the whole gesture regardless of
    // minor drift.
    lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(btn, cb, code, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);
    return btn;
}

// Button labels bumped from LVGL's ~14px default to 18px -- matches the
// same "hard to read at the default size" fix already applied on the
// Circle of Fifths screen.
static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int w, int h, int x, int y,
                              lv_event_cb_t cb, void *user_data)
{
    // SHORT_CLICKED, not CLICKED: harmless for buttons with no long-press
    // handler, but required for the Start/Stop button below, which now has
    // one -- CLICKED still fires on release even after a long press, which
    // would immediately toggle start/stop right after the time-signature
    // dialog opens or closes.
    return make_button_ex(parent, text, w, h, x, y, LV_EVENT_SHORT_CLICKED, cb, user_data, &lv_font_dejavu_18);
}

// LV_EVENT_CLICKED still fires on release even after a long press, so using
// it for the single-step tap would double-count the last step whenever a
// hold-to-repeat sequence just finished. LV_EVENT_SHORT_CLICKED only fires
// when no long press happened, so it's the correct pairing with
// LV_EVENT_LONG_PRESSED_REPEAT for hold-to-repeat.
//
// Font: was 24px; asked for triple (72px), but LVGL's bundled Montserrat
// set tops out at 48px (2x) -- using the largest available rather than a
// custom-built font for one exact multiple.
static lv_obj_t *make_bpm_button(lv_obj_t *parent, const char *text, int w, int h, int x, int y, int step)
{
    void *user_data = (void *)(intptr_t)step;
    lv_obj_t *btn = make_button_ex(parent, text, w, h, x, y, LV_EVENT_SHORT_CLICKED, bpm_step_event_cb, user_data,
                                    &lv_font_dejavu_48);
    lv_obj_add_event_cb(btn, bpm_step_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, user_data);
    return btn;
}

static void numpad_refresh_display(void)
{
    lv_label_set_text(s_numpad_entry_label, s_numpad_entry[0] == '\0' ? "___" : s_numpad_entry);
}

static void close_numpad(void)
{
    if (s_numpad_overlay) {
        lv_obj_delete(s_numpad_overlay);
        s_numpad_overlay = NULL;
        s_numpad_entry_label = NULL;
    }
    s_numpad_open = false;
}

static void numpad_digit_cb(lv_event_t *e)
{
    int digit = (int)(intptr_t)lv_event_get_user_data(e);
    size_t len = strlen(s_numpad_entry);
    if (len < 3) {
        s_numpad_entry[len] = (char)('0' + digit);
        s_numpad_entry[len + 1] = '\0';
    }
    numpad_refresh_display();
}

static void numpad_cancel_cb(lv_event_t *e)
{
    close_numpad();
}

static void numpad_enter_cb(lv_event_t *e)
{
    if (s_numpad_entry[0] != '\0') {
        Metronome_SetBPM((uint16_t)atoi(s_numpad_entry));
        refresh_bpm_label();
    }
    close_numpad();
}

static void show_numpad_dialog(void)
{
    if (s_numpad_open) {
        return;
    }
    s_numpad_open = true;
    s_numpad_entry[0] = '\0';

    s_numpad_overlay = lv_obj_create(s_screen);
    lv_obj_set_size(s_numpad_overlay, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_numpad_overlay, 0, 0);
    lv_obj_remove_flag(s_numpad_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_numpad_overlay, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_numpad_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_numpad_overlay, 0, 0);
    lv_obj_set_style_radius(s_numpad_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_numpad_overlay, 0, 0);

    lv_obj_t *title = lv_label_create(s_numpad_overlay);
    lv_label_set_text(title, "Enter BPM");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_dejavu_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 95);

    s_numpad_entry_label = lv_label_create(s_numpad_overlay);
    lv_obj_set_style_text_color(s_numpad_entry_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_numpad_entry_label, &lv_font_dejavu_32, 0);
    lv_obj_align(s_numpad_entry_label, LV_ALIGN_TOP_MID, 0, 122);
    numpad_refresh_display();

    // Standard 3x4 keypad: 1-9, then Cancel/0/OK.
    static const char *KEY_LABELS[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "Cancel", "0", "OK" };
    const int grid_w = 80, grid_h = 50, gap = 10;
    const int start_x = SCREEN_CENTER_X - (grid_w * 3 + gap * 2) / 2;
    const int start_y = 175;

    for (int i = 0; i < 12; i++) {
        int row = i / 3, col = i % 3;
        int x = start_x + col * (grid_w + gap);
        int y = start_y + row * (grid_h + gap);

        lv_event_cb_t cb;
        void *user_data = NULL;
        if (i == 9) {
            cb = numpad_cancel_cb;
        } else if (i == 11) {
            cb = numpad_enter_cb;
        } else {
            cb = numpad_digit_cb;
            user_data = (void *)(intptr_t)(i == 10 ? 0 : i + 1); // i=0..8 -> 1..9, i=10 -> 0
        }

        lv_obj_t *btn = lv_button_create(s_numpad_overlay);
        lv_obj_set_size(btn, grid_w, grid_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BTN), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, KEY_LABELS[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_dejavu_18, 0);
        lv_obj_center(lbl);
    }
}

static void bpm_label_long_press_cb(lv_event_t *e)
{
    show_numpad_dialog();
}

static void close_timesig_dialog(void)
{
    if (s_timesig_overlay) {
        lv_obj_delete(s_timesig_overlay);
        s_timesig_overlay = NULL;
    }
    s_timesig_open = false;
}

static void timesig_select_cb(lv_event_t *e)
{
    int beats = (int)(intptr_t)lv_event_get_user_data(e);
    Metronome_SetBeatsPerMeasure((uint8_t)beats);
    close_timesig_dialog();
}

static void timesig_cancel_cb(lv_event_t *e)
{
    close_timesig_dialog();
}

static void show_timesig_dialog(void)
{
    if (s_timesig_open) {
        return;
    }
    s_timesig_open = true;

    s_timesig_overlay = lv_obj_create(s_screen);
    lv_obj_set_size(s_timesig_overlay, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_timesig_overlay, 0, 0);
    lv_obj_remove_flag(s_timesig_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_timesig_overlay, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_timesig_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_timesig_overlay, 0, 0);
    lv_obj_set_style_radius(s_timesig_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_timesig_overlay, 0, 0);

    lv_obj_t *title = lv_label_create(s_timesig_overlay);
    lv_label_set_text(title, "Time Signature");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_dejavu_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 95);

    // { label, beats-per-measure }. Compound 6/8 is treated the same as a
    // simple 6-beat measure here (one click per eighth note, downbeat
    // accent on the first) rather than a true 2-groups-of-3 compound feel
    // -- the engine only tracks a flat beat count, not subdivision.
    static const struct { const char *label; int beats; } SIGNATURES[3] = {
        { "4/4", 4 },
        { "3/4", 3 },
        { "6/8", 6 },
    };
    uint8_t current_beats = Metronome_GetBeatsPerMeasure();

    const int btn_w = 200, btn_h = 66, pitch = 78;
    const int start_y = 130;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(s_timesig_overlay);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, SCREEN_CENTER_X - btn_w / 2, start_y + i * pitch);
        lv_obj_set_style_bg_color(btn,
                                   lv_color_hex(SIGNATURES[i].beats == current_beats ? COLOR_BTN_ACTIVE : COLOR_BTN),
                                   0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, timesig_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SIGNATURES[i].beats);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, SIGNATURES[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_dejavu_18, 0);
        lv_obj_center(lbl);
    }

    lv_obj_t *cancel_btn = lv_button_create(s_timesig_overlay);
    lv_obj_set_size(cancel_btn, 140, 56);
    lv_obj_set_pos(cancel_btn, SCREEN_CENTER_X - 70, start_y + 3 * pitch);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(COLOR_BTN), 0);
    lv_obj_set_style_radius(cancel_btn, 12, 0);
    lv_obj_set_style_border_width(cancel_btn, 0, 0);
    lv_obj_add_event_cb(cancel_btn, timesig_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_dejavu_18, 0);
    lv_obj_center(cancel_lbl);
}

static void start_stop_long_press_cb(lv_event_t *e)
{
    show_timesig_dialog();
}

// A short radial tick at the given swing angle (0.1-degree units, same
// convention as the needle's transform_rotation: 0 = straight up, positive
// = clockwise). Drawn vertical at MARKER_RADIUS above center, then rotated
// in place so it ends up lying along the radius at that angle.
static lv_obj_t *make_swing_marker(lv_obj_t *parent, int32_t angle_tenths_deg)
{
    float theta = (angle_tenths_deg / 10.0f) * ((float)M_PI / 180.0f);
    int cx = SCREEN_CENTER_X + (int)lroundf(MARKER_RADIUS * sinf(theta));
    int cy = SCREEN_CENTER_Y - (int)lroundf(MARKER_RADIUS * cosf(theta));

    lv_obj_t *marker = lv_obj_create(parent);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(marker, MARKER_WIDTH, MARKER_LENGTH);
    lv_obj_set_pos(marker, cx - MARKER_WIDTH / 2, cy - MARKER_LENGTH / 2);
    lv_obj_set_style_radius(marker, MARKER_WIDTH / 2, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(COLOR_MARKER), 0);
    lv_obj_set_style_border_width(marker, 0, 0);
    lv_obj_set_style_transform_rotation(marker, angle_tenths_deg, 0);
    return marker;
}

void MetronomeUI_Create(lv_obj_t *parent)
{
    lv_obj_t *screen = parent;
    s_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* ---- beat ring, near the physical edge ----
     * Originally built as 4 separate persistently-colored arc segments (one
     * per beat), but measured on hardware: LVGL invalidates a widget's full
     * bounding box on any style change, and each segment's box was the full
     * ring diameter (they all overlap, being centered/same-sized). Any one
     * segment lighting up forced a recomposite of all 4 stacked full-size
     * arcs -- ~300ms per redraw, which is why the ring visibly lagged the
     * click. Dropping to a single arc cut that to ~40-60ms. So: one arc,
     * cumulative fill (jumps straight to the next quarter each beat, no
     * animating the fill over the beat's duration -- that animation was
     * also driving repeated expensive redraws), recolored gold in real
     * time for whichever beat is currently the downbeat's fill step. */
    s_ring = lv_arc_create(screen);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_ring, RING_DIAMETER, RING_DIAMETER);
    lv_obj_center(s_ring);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_rotation(s_ring, 270); // 0 = 12 o'clock, matches the needle's neutral position
    lv_arc_set_range(s_ring, 0, 100);
    lv_arc_set_value(s_ring, 0);
    lv_obj_set_style_arc_width(s_ring, RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(COLOR_RING_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(COLOR_ACCENT_OTHER), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_ring, false, LV_PART_INDICATOR);

    /* ---- pendulum needle, pivoting at screen center ----
     * At the wide 45-degree swing this needle can visually pass under the
     * BPM stack and +/- buttons created further down. LVGL draws siblings
     * in creation order (later = on top), and everything the needle could
     * collide with is created after it, so it's already at a lower
     * z-level than those controls without any extra work here -- keep the
     * needle/marker creation above the interactive controls below. */
    s_needle = lv_obj_create(screen);
    lv_obj_remove_flag(s_needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_needle, NEEDLE_WIDTH, NEEDLE_HEIGHT);
    lv_obj_set_pos(s_needle, SCREEN_CENTER_X - NEEDLE_WIDTH / 2, SCREEN_CENTER_Y - NEEDLE_HEIGHT);
    lv_obj_set_style_radius(s_needle, NEEDLE_WIDTH / 2, 0);
    lv_obj_set_style_bg_color(s_needle, lv_color_hex(COLOR_NEEDLE), 0);
    lv_obj_set_style_border_width(s_needle, 0, 0);
    lv_obj_set_style_transform_pivot_x(s_needle, NEEDLE_WIDTH / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_needle, NEEDLE_HEIGHT, 0);

    /* ---- pendulum swing endpoint markers, near the display's edge ---- */
    s_marker_left = make_swing_marker(screen, -NEEDLE_MAX_ANGLE);
    s_marker_right = make_swing_marker(screen, NEEDLE_MAX_ANGLE);

    /* ---- BPM readout + tempo steppers ----
     * Two lines stacked in the gap between the +/- buttons: the value at
     * the largest available bundled font (48px, same cap as the +/-
     * button digits), "BPM" underneath at a smaller size. A separate
     * transparent object (not the labels themselves) owns the long-press
     * handler so tapping either line opens the numeric-entry dialog. */
    s_bpm_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_bpm_value_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_bpm_value_label, &lv_font_dejavu_48, 0);
    lv_obj_align(s_bpm_value_label, LV_ALIGN_TOP_MID, 0, BPM_STACK_Y);
    refresh_bpm_label();

    lv_obj_t *bpm_unit_label = lv_label_create(screen);
    lv_label_set_text(bpm_unit_label, "BPM");
    lv_obj_set_style_text_color(bpm_unit_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(bpm_unit_label, &lv_font_dejavu_18, 0);
    lv_obj_align(bpm_unit_label, LV_ALIGN_TOP_MID, 0, BPM_STACK_Y + 60);

    lv_obj_t *bpm_hit_area = lv_obj_create(screen);
    lv_obj_set_size(bpm_hit_area, BPM_STACK_WIDTH, 82);
    lv_obj_align(bpm_hit_area, LV_ALIGN_TOP_MID, 0, BPM_STACK_Y);
    lv_obj_remove_flag(bpm_hit_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bpm_hit_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bpm_hit_area, 0, 0);
    lv_obj_add_event_cb(bpm_hit_area, bpm_label_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    // SHORT_CLICKED (not CLICKED) so a long press that opens the numpad
    // doesn't also register as a tap-tempo hit on release.
    lv_obj_add_event_cb(bpm_hit_area, tap_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    // All buttons below are ~33% larger than the original sizes for easier
    // touch targets; positions were recomputed (not just scaled in place)
    // so the bigger boxes still clear the round bezel and don't collide
    // with each other -- corners stay within roughly the same ~190px
    // safe radius the smaller buttons used.
    // Hold either button to repeat past a single BPM per tap; long-press
    // the BPM readout above to open a numeric-entry dialog instead.
    make_bpm_button(screen, "-", BPM_BTN_SIZE, BPM_BTN_SIZE, MINUS_BTN_X, MINUS_BTN_Y, -1);
    make_bpm_button(screen, "+", BPM_BTN_SIZE, BPM_BTN_SIZE, PLUS_BTN_X, PLUS_BTN_Y, 1);

    /* ---- display-mode selector: pendulum or ring, not both at once ----
     * y=241 keeps the row's top edge just below the screen's vertical
     * midpoint (240), per request -- all four lower buttons should sit
     * below center, not straddle it. */
    lv_obj_t *mode_row = lv_obj_create(screen);
    lv_obj_remove_flag(mode_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(mode_row, 340, 58);
    lv_obj_set_style_bg_opa(mode_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mode_row, 0, 0);
    lv_obj_set_style_pad_all(mode_row, 0, 0);
    lv_obj_align(mode_row, LV_ALIGN_TOP_MID, 0, 241);

    // Icons instead of text, and pulled in close to the switch itself (not
    // pinned to the row's far edges) per request -- half the switch's own
    // width (35) + half an icon's width (~16) + a small gap (~9).
    s_mode_label_ring = lv_label_create(mode_row);
    lv_label_set_text(s_mode_label_ring, ICON_CIRCLE_DASHED);
    lv_obj_set_style_text_font(s_mode_label_ring, &lv_font_tabler_icons_32, 0);
    lv_obj_align(s_mode_label_ring, LV_ALIGN_CENTER, -60, 0);

    s_mode_switch = lv_switch_create(mode_row);
    lv_obj_set_size(s_mode_switch, 70, 36);
    lv_obj_align(s_mode_switch, LV_ALIGN_CENTER, 0, 0);
    // Without this, a swipe that starts on the switch never reaches the
    // screen-level gesture handler -- same reasoning as make_button_ex's
    // own LV_OBJ_FLAG_GESTURE_BUBBLE above.
    lv_obj_add_flag(s_mode_switch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(s_mode_switch, lv_color_hex(COLOR_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_mode_switch, lv_color_hex(COLOR_BTN_ACTIVE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_mode_switch, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    // The knob-slide animation is driven by this style's anim_duration (see
    // lv_switch_trigger_anim), not by a style transition -- the theme's
    // default 120ms read as an instant snap rather than a visible slide.
    lv_obj_set_style_anim_duration(s_mode_switch, 220, LV_PART_MAIN);
    lv_obj_add_event_cb(s_mode_switch, mode_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_mode_label_pendulum = lv_label_create(mode_row);
    lv_label_set_text(s_mode_label_pendulum, ICON_METRONOME);
    lv_obj_set_style_text_font(s_mode_label_pendulum, &lv_font_tabler_icons_32, 0);
    lv_obj_align(s_mode_label_pendulum, LV_ALIGN_CENTER, 60, 0);

    /* ---- start/stop ----
     * Tap tempo now lives on the BPM readout itself (see bpm_hit_area's
     * SHORT_CLICKED handler above), so this button gets the full row. */
    s_start_stop_btn = make_button(screen, "", 260, 72, SCREEN_CENTER_X - 130, 314, start_stop_event_cb, NULL);
    s_start_stop_label = lv_obj_get_child(s_start_stop_btn, 0);
    lv_label_set_text(s_start_stop_label, LV_SYMBOL_PLAY " START");
    lv_obj_set_style_bg_color(s_start_stop_btn, lv_color_hex(COLOR_BTN_START), 0);
    lv_obj_add_event_cb(s_start_stop_btn, start_stop_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    reset_visuals();
    apply_mode_visibility();
}

bool MetronomeUI_IsModalOpen(void)
{
    return s_numpad_open || s_timesig_open;
}

static void needle_angle_anim_cb(void *var, int32_t value)
{
    lv_obj_set_style_transform_rotation((lv_obj_t *)var, value, 0);
}

void MetronomeUI_OnBeat(uint8_t beat_index, bool is_downbeat, uint16_t beat_period_ms)
{
    /* Pendulum: one full swing to the opposite extreme per beat. */
    lv_anim_delete(s_needle, NULL);
    int32_t from = s_needle_going_right ? -NEEDLE_MAX_ANGLE : NEEDLE_MAX_ANGLE;
    int32_t to = s_needle_going_right ? NEEDLE_MAX_ANGLE : -NEEDLE_MAX_ANGLE;
    s_needle_going_right = !s_needle_going_right;

    lv_anim_t needle_anim;
    lv_anim_init(&needle_anim);
    lv_anim_set_var(&needle_anim, s_needle);
    lv_anim_set_exec_cb(&needle_anim, needle_angle_anim_cb);
    lv_anim_set_values(&needle_anim, from, to);
    lv_anim_set_duration(&needle_anim, beat_period_ms);
    lv_anim_set_path_cb(&needle_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&needle_anim);

    /* Ring: cumulative fill, jumps straight to the next fraction -- no
     * animation, no per-segment widgets. See the comment in
     * MetronomeUI_Create for why. */
    uint8_t total_beats = Metronome_GetBeatsPerMeasure();
    if (total_beats < 1) {
        total_beats = 1;
    }
    if (is_downbeat) {
        lv_arc_set_value(s_ring, 0);
    }
    int32_t value = (int32_t)(beat_index + 1) * 100 / total_beats;
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(is_downbeat ? COLOR_ACCENT_BEAT : COLOR_ACCENT_OTHER),
                                LV_PART_INDICATOR);
    lv_arc_set_value(s_ring, value);
}
