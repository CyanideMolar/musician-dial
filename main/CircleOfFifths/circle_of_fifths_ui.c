#include "circle_of_fifths_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Generated from LVGL's own Montserrat-Bold.ttf test asset (lv_font_conv,
// 18px/4bpp, ASCII 0x20-0x7E) -- none of the bundled Kconfig fonts ship a
// bold weight, so this is a real bold font rather than a synthetic
// double-draw effect.
LV_FONT_DECLARE(lv_font_montserrat_18_bold);

// DejaVu Sans (bundled with LVGL's own repo, lv_font_conv'd at 18/28px)
// instead of Montserrat for regular-weight text: Montserrat's "G" has only
// a thin spur to tell it apart from "C", which gets lost at these sizes on
// this display; DejaVu's is much more open and legible small.
LV_FONT_DECLARE(lv_font_dejavu_18);
LV_FONT_DECLARE(lv_font_dejavu_28);

/* Same 480x480 round-display layout rules as the metronome screen: content
 * kept within a safe radius of center so nothing sits under the round
 * bezel. The metronome's beat ring (radius 228) reads fine on hardware, so
 * that's treated as the known-good outer limit; this wheel stays inside it
 * with margin. */
#define SCREEN_CENTER_X 240
#define SCREEN_CENTER_Y 240

#define WHEEL_RADIUS   175
#define KEY_BTN_SIZE   60
#define NUM_KEYS       12

// The selection indicator (see s_selection_square) sits behind whichever
// key button is selected, sized a bit larger so its corners peek out from
// behind the button's circular outline.
#define SELECTION_SQUARE_SIZE (KEY_BTN_SIZE + 16)

#define COLOR_BG          0x101418
#define COLOR_SELECTED    0xFFC857 // same gold as the metronome's downbeat accent
#define COLOR_TEXT_DARK   0x101418 // for buttons whose fill is light
#define COLOR_TEXT_LIGHT  0xE8E8E8 // for buttons whose fill is dark
#define COLOR_ACCENT      0x4FD1C5 // same teal as the metronome's regular-beat accent

typedef struct {
    const char *major;
    const char *minor;
    int8_t accidentals; // positive = sharps, negative = flats, 0 = natural
} cof_key_t;

// Circle-of-fifths order starting at C (12 o'clock), going clockwise by
// perfect fifths. IV and V of key i are simply the neighboring entries
// (one step counter-clockwise / clockwise); no separate chord table needed.
static const cof_key_t COF_KEYS[NUM_KEYS] = {
    { "C",  "Am",   0 },
    { "G",  "Em",   1 },
    { "D",  "Bm",   2 },
    { "A",  "F#m",  3 },
    { "E",  "C#m",  4 },
    { "B",  "G#m",  5 },
    { "Gb", "Ebm", -6 },
    { "Db", "Bbm", -5 },
    { "Ab", "Fm",  -4 },
    { "Eb", "Cm",  -3 },
    { "Bb", "Gm",  -2 },
    { "F",  "Dm",  -1 },
};

// Per-key wheel-segment colors, in the same clockwise-from-C order as
// COF_KEYS above.
static const uint32_t COF_KEY_COLORS[NUM_KEYS] = {
    0xee3c3a, 0xeaa323, 0xffd21f, 0x9ccc68,
    0x669a69, 0x49bdbd, 0x48c7f4, 0x525d9b,
    0x2d3194, 0x9e5ba0, 0xc769a7, 0xea3c95,
};

// Per-key label text color, picked per COF_KEY_COLORS entry by WCAG
// contrast ratio (against black 0x101418 vs. white 0xE8E8E8 -- the same
// near-black/near-white shades used elsewhere in this UI, not pure
// 0x000000/0xFFFFFF). Only the three darkest fills (Db, Ab, Eb) come out
// ahead with light text; every other key reads better with dark text, even
// the saturated ones like C's red or Gb's light blue.
static const uint32_t COF_KEY_TEXT_COLORS[NUM_KEYS] = {
    COLOR_TEXT_DARK,  // C
    COLOR_TEXT_DARK,  // G
    COLOR_TEXT_DARK,  // D
    COLOR_TEXT_DARK,  // A
    COLOR_TEXT_DARK,  // E
    COLOR_TEXT_DARK,  // B
    COLOR_TEXT_DARK,  // Gb
    COLOR_TEXT_LIGHT, // Db
    COLOR_TEXT_LIGHT, // Ab
    COLOR_TEXT_LIGHT, // Eb
    COLOR_TEXT_DARK,  // Bb
    COLOR_TEXT_DARK,  // F
};

static lv_obj_t *s_key_btn[NUM_KEYS];
static lv_obj_t *s_selection_square;
static int s_key_cx[NUM_KEYS];
static int s_key_cy[NUM_KEYS];
static lv_obj_t *s_title_label;
static lv_obj_t *s_relative_label;
static lv_obj_t *s_signature_label;
static lv_obj_t *s_notes_label;
// Each chord line is split into two labels side by side in a flex row --
// the Nashville-numeral prefix at the regular weight, the actual chord
// names bold -- rather than one label, since LVGL labels can only have a
// single font each.
static lv_obj_t *s_chords_prefix_label;
static lv_obj_t *s_chords_bold_label;
static lv_obj_t *s_chords2_prefix_label;
static lv_obj_t *s_chords2_bold_label;
static int s_selected = 0;
static bool s_showing_minor = false;

static void format_signature(int8_t acc, char *buf, size_t len)
{
    if (acc == 0) {
        snprintf(buf, len, "No sharps or flats");
    } else if (acc > 0) {
        snprintf(buf, len, "%d sharp%s", acc, acc == 1 ? "" : "s");
    } else {
        snprintf(buf, len, "%d flat%s", -acc, acc == -1 ? "" : "s");
    }
}

// Builds the 7-note diatonic scale for any tonic letter + key signature.
// Works for both major and natural minor: which letters get sharped or
// flatted depends only on the signature (traditional sharp order FCGDAEB,
// flat order BEADGCF), not on which degree the scale starts from, so the
// same routine covers "C major's notes" and "A (its relative minor)'s
// notes" by just passing a different tonic letter with the same signature.
static void build_scale(char tonic_letter, int8_t accidentals, char *buf, size_t len)
{
    static const char SHARP_ORDER[] = "FCGDAEB";
    static const char FLAT_ORDER[] = "BEADGCF";
    bool sharp[7] = { 0 };
    bool flat[7] = { 0 };

    if (accidentals > 0) {
        for (int i = 0; i < accidentals; i++) {
            sharp[SHARP_ORDER[i] - 'A'] = true;
        }
    } else if (accidentals < 0) {
        for (int i = 0; i < -accidentals; i++) {
            flat[FLAT_ORDER[i] - 'A'] = true;
        }
    }

    int start = tonic_letter - 'A';
    size_t pos = 0;
    for (int i = 0; i < 7 && pos < len; i++) {
        int idx = (start + i) % 7;
        char letter = (char)('A' + idx);
        int n = snprintf(buf + pos, len - pos, i == 0 ? "%c%s" : " %c%s", letter,
                          sharp[idx] ? "#" : (flat[idx] ? "b" : ""));
        if (n > 0) {
            pos += (size_t)n;
        }
    }
}

static void select_key(int index)
{
    s_selected = index;

    // Selection is shown by the gold square behind the button, not by
    // recoloring the button itself -- each key keeps its own wheel color
    // and its own contrast-picked text color at all times.
    lv_obj_set_pos(s_selection_square, s_key_cx[index] - SELECTION_SQUARE_SIZE / 2,
                   s_key_cy[index] - SELECTION_SQUARE_SIZE / 2);

    const cof_key_t *key = &COF_KEYS[index];
    char buf[48];

    if (s_showing_minor) {
        /* Parallel minor (same tonic, e.g. tapping A shows A major but this
         * shows A minor) -- a different key from A major's relative minor
         * (F#m), with its own signature and chords. Music theory fact used
         * here: key X's parallel minor has the same signature/chords as
         * the relative minor of the major key sitting 3 positions
         * counter-clockwise from X on this wheel (== C major's slot for
         * X=A, since A minor and C major share 0 sharps/flats). */
        int base = (index + NUM_KEYS - 3) % NUM_KEYS;
        const char *iv = COF_KEYS[(base + NUM_KEYS - 1) % NUM_KEYS].minor;
        const char *v = COF_KEYS[(base + 1) % NUM_KEYS].minor;
        const char *relative_major = COF_KEYS[base].major;
        // Natural minor's remaining non-diminished triads (everything but
        // i/III/iv/v above and the diminished ii): VI and VII are just IV
        // and V of the relative major, i.e. the .major of the same two
        // wheel slots used for iv/v above (which pull .minor there).
        const char *chord_VI = COF_KEYS[(base + NUM_KEYS - 1) % NUM_KEYS].major;
        const char *chord_VII = COF_KEYS[(base + 1) % NUM_KEYS].major;

        snprintf(buf, sizeof(buf), "%s Minor", key->major);
        lv_label_set_text(s_title_label, buf);

        snprintf(buf, sizeof(buf), "relative major: %s", relative_major);
        lv_label_set_text(s_relative_label, buf);

        format_signature(COF_KEYS[base].accidentals, buf, sizeof(buf));
        lv_label_set_text(s_signature_label, buf);

        build_scale(key->major[0], COF_KEYS[base].accidentals, buf, sizeof(buf));
        lv_label_set_text(s_notes_label, buf);

        lv_label_set_text(s_chords_prefix_label, "i-iv-v-III:");
        snprintf(buf, sizeof(buf), "%sm %s %s %s", key->major, iv, v, relative_major);
        lv_label_set_text(s_chords_bold_label, buf);

        lv_label_set_text(s_chords2_prefix_label, "VI-VII:");
        snprintf(buf, sizeof(buf), "%s %s", chord_VI, chord_VII);
        lv_label_set_text(s_chords2_bold_label, buf);
    } else {
        const char *chord_iv = COF_KEYS[(index + NUM_KEYS - 1) % NUM_KEYS].major;
        const char *chord_v = COF_KEYS[(index + 1) % NUM_KEYS].major;
        // Remaining non-diminished triads: ii is the relative minor of IV,
        // iii is the relative minor of V (same wheel slots as chord_iv/v,
        // just reading .minor instead of .major).
        const char *chord_ii = COF_KEYS[(index + NUM_KEYS - 1) % NUM_KEYS].minor;
        const char *chord_iii = COF_KEYS[(index + 1) % NUM_KEYS].minor;

        snprintf(buf, sizeof(buf), "%s Major", key->major);
        lv_label_set_text(s_title_label, buf);

        snprintf(buf, sizeof(buf), "relative: %s minor", key->minor);
        lv_label_set_text(s_relative_label, buf);

        format_signature(key->accidentals, buf, sizeof(buf));
        lv_label_set_text(s_signature_label, buf);

        build_scale(key->major[0], key->accidentals, buf, sizeof(buf));
        lv_label_set_text(s_notes_label, buf);

        lv_label_set_text(s_chords_prefix_label, "I-IV-V-vi:");
        snprintf(buf, sizeof(buf), "%s %s %s %s", key->major, chord_iv, chord_v, key->minor);
        lv_label_set_text(s_chords_bold_label, buf);

        lv_label_set_text(s_chords2_prefix_label, "ii-iii:");
        snprintf(buf, sizeof(buf), "%s %s", chord_ii, chord_iii);
        lv_label_set_text(s_chords2_bold_label, buf);
    }
}

static void key_tap_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    s_showing_minor = false;
    select_key(index);
}

static void key_long_press_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    s_showing_minor = true;
    select_key(index);
}

static lv_obj_t *make_key_button(lv_obj_t *parent, const char *text, int cx, int cy, int index)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, KEY_BTN_SIZE, KEY_BTN_SIZE);
    lv_obj_set_pos(btn, cx - KEY_BTN_SIZE / 2, cy - KEY_BTN_SIZE / 2);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COF_KEY_COLORS[index]), 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    // Without this, a swipe starting on a wheel button never reaches the
    // screen-level gesture handler (LVGL only sends LV_EVENT_GESTURE to the
    // exact widget touched, bubbling only if it opts in). Harmless for the
    // long-press-for-minor gesture below: a stationary long-press never
    // accumulates enough movement to cross the gesture-detection threshold
    // in the first place, bubble flag or not.
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // SHORT_CLICKED (not CLICKED) for the tap case: CLICKED still fires on
    // release even after a long press, which would flip back to major right
    // after showing minor. SHORT_CLICKED only fires when no long press
    // happened, so it pairs correctly with LONG_PRESSED here.
    void *user_data = (void *)(intptr_t)index;
    lv_obj_add_event_cb(btn, key_tap_cb, LV_EVENT_SHORT_CLICKED, user_data);
    lv_obj_add_event_cb(btn, key_long_press_cb, LV_EVENT_LONG_PRESSED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COF_KEY_TEXT_COLORS[index]), 0);
    lv_obj_set_style_text_font(label, &lv_font_dejavu_28, 0); // ~100% larger than the ~14px default
    lv_obj_center(label);
    return btn;
}

// One chord line: a regular-weight Nashville-numeral prefix and a bold
// chord-name label side by side, in a content-sized flex row so the pair
// stays centered as a unit regardless of how wide either piece is (chord
// lists vary a lot in width across keys).
static void make_chord_row(lv_obj_t *parent, int y_offset, lv_obj_t **prefix_out, lv_obj_t **bold_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, y_offset);

    lv_obj_t *prefix = lv_label_create(row);
    lv_obj_set_style_text_color(prefix, lv_color_hex(COLOR_TEXT_LIGHT), 0);
    lv_obj_set_style_text_font(prefix, &lv_font_dejavu_18, 0);

    lv_obj_t *bold = lv_label_create(row);
    lv_obj_set_style_text_color(bold, lv_color_hex(COLOR_TEXT_LIGHT), 0);
    lv_obj_set_style_text_font(bold, &lv_font_montserrat_18_bold, 0);

    *prefix_out = prefix;
    *bold_out = bold;
}

void CircleOfFifthsUI_Create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // Created before the key buttons below so it lands earlier in the
    // parent's child list -- LVGL draws children in that order, so it
    // renders behind every button without needing an explicit z-order
    // call. Position gets set for real by the select_key(0) call at the
    // bottom of this function.
    s_selection_square = lv_obj_create(parent);
    lv_obj_set_size(s_selection_square, SELECTION_SQUARE_SIZE, SELECTION_SQUARE_SIZE);
    lv_obj_remove_flag(s_selection_square, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_selection_square, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_selection_square, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_selection_square, 0, 0);
    lv_obj_set_style_border_color(s_selection_square, lv_color_hex(COLOR_SELECTED), 0);
    lv_obj_set_style_border_width(s_selection_square, 4, 0);

    /* Twelve small, non-overlapping buttons around the wheel -- each is
     * cheap to redraw on its own. The metronome screen's earlier design
     * used a handful of large overlapping full-diameter arc widgets for a
     * similar "ring of things" idea, and that turned out to cost ~300ms
     * per redraw because LVGL recomposites a widget's whole bounding box
     * on any change, including its overlapping siblings. Small buttons
     * with real gaps between them don't have that problem. */
    for (int i = 0; i < NUM_KEYS; i++) {
        float theta = i * (2.0f * (float)M_PI / NUM_KEYS);
        int cx = SCREEN_CENTER_X + (int)lroundf(WHEEL_RADIUS * sinf(theta));
        int cy = SCREEN_CENTER_Y - (int)lroundf(WHEEL_RADIUS * cosf(theta));
        s_key_cx[i] = cx;
        s_key_cy[i] = cy;
        s_key_btn[i] = make_key_button(parent, COF_KEYS[i].major, cx, cy, i);
    }

    s_title_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(COLOR_TEXT_LIGHT), 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_dejavu_28, 0);
    lv_obj_align(s_title_label, LV_ALIGN_CENTER, 0, -70);

    // Body lines bumped from the default (~14px, "hard to read") to 18px.
    s_relative_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_relative_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(s_relative_label, &lv_font_dejavu_18, 0);
    lv_obj_align(s_relative_label, LV_ALIGN_CENTER, 0, -32);

    s_signature_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_signature_label, lv_color_hex(COLOR_TEXT_LIGHT), 0);
    lv_obj_set_style_text_font(s_signature_label, &lv_font_dejavu_18, 0);
    lv_obj_align(s_signature_label, LV_ALIGN_CENTER, 0, -4);

    s_notes_label = lv_label_create(parent);
    lv_obj_set_style_text_color(s_notes_label, lv_color_hex(COLOR_TEXT_LIGHT), 0);
    lv_obj_set_style_text_font(s_notes_label, &lv_font_dejavu_18, 0);
    lv_obj_align(s_notes_label, LV_ALIGN_CENTER, 0, 24);

    make_chord_row(parent, 52, &s_chords_prefix_label, &s_chords_bold_label);

    // The remaining non-diminished triads left out of the primary
    // progression above (major: ii/iii; minor: VI/VII) -- vii degree
    // (diminished) is intentionally excluded from both.
    make_chord_row(parent, 80, &s_chords2_prefix_label, &s_chords2_bold_label);

    select_key(0); // default to C major
}
