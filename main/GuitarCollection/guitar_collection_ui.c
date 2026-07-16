#include "guitar_collection_ui.h"
#include "guitar_collection.h"
#include "pairing.h"
#include "wifi_provisioning.h"
#include "practice_session.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_dejavu_18);
LV_FONT_DECLARE(lv_font_dejavu_32);

#define SCREEN_CENTER_X 240

// Same palette as metronome_ui.c -- kept in sync by eye, not shared code,
// matching how every UI module in this codebase already duplicates it.
#define COLOR_BG           0x101418
#define COLOR_TEXT         0xE8E8E8
#define COLOR_BTN          0x1F252C
#define COLOR_BTN_ACTIVE    0x3A4552
#define COLOR_BTN_START    0x2E7D32
#define COLOR_BTN_STOP     0xB3261E
#define COLOR_ACCENT_OTHER 0x4FD1C5

static lv_obj_t *s_root;
static lv_obj_t *s_name_label;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_practice_btn;
static lv_obj_t *s_practice_btn_label;
static lv_obj_t *s_hint_label;

static int s_current_index = 0;
static bool s_was_active = false;
static uint32_t s_ticks_since_last_attempt = 0;
static int s_refresh_attempts = 0;
static int s_last_rendered_index = -1;

typedef enum { CARD_BTN_START, CARD_BTN_END, CARD_BTN_SWITCH, CARD_BTN_HIDDEN } card_btn_kind_t;
static card_btn_kind_t s_last_rendered_btn_kind = (card_btn_kind_t)-1;
static uint32_t s_last_rendered_seconds = UINT32_MAX;

// ---------------------------------------------------------------------------
// Review dialog (End Practice -> rate -> Log or Forget)
// ---------------------------------------------------------------------------

static lv_obj_t *s_review_overlay;
static lv_obj_t *s_review_total_label;
static lv_obj_t *s_review_breakdown_label;
static lv_obj_t *s_review_rating_row;
static lv_obj_t *s_review_rating_btns[5];
static lv_obj_t *s_review_log_btn;
static lv_obj_t *s_review_forget_btn;
static lv_obj_t *s_review_confirm_label;
static lv_obj_t *s_review_confirm_discard_btn;
static lv_obj_t *s_review_confirm_cancel_btn;

static bool s_review_open = false;
static int s_selected_rating = 0; // 0 = none chosen yet

// ~30s at the main loop's 5ms tick (see main.c) between retry attempts if a
// refresh fails -- e.g. the first attempt racing WiFi still connecting. Each
// attempt is a real HTTPS+TLS round trip, not free, so this stays well
// clear of anything that would visibly compete with LVGL's render loop.
#define RETRY_INTERVAL_TICKS 6000
// Give up after this many failed attempts (~2.5 minutes) rather than
// retrying forever if something's persistently wrong (bad key, DNS, etc).
#define MAX_AUTO_RETRIES 5

static bool is_active(void)
{
    // Pairing_Init() sets PAIRED from NVS immediately at boot regardless of
    // whether WiFi has actually finished connecting yet -- gate on both, not
    // just pairing state, or the very first refresh attempt races the
    // network coming up and fails with nothing to retry it.
    return Pairing_GetState() == PAIRING_STATE_PAIRED &&
           WiFiProvisioning_GetState() == WIFI_PROV_STATE_CONNECTED;
}

static void format_hms(uint32_t total_seconds, char *out, size_t out_len)
{
    uint32_t h = total_seconds / 3600;
    uint32_t m = (total_seconds % 3600) / 60;
    uint32_t s = total_seconds % 60;
    snprintf(out, out_len, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
}

static void refresh_rating_buttons_highlight(void)
{
    for (int i = 0; i < 5; i++) {
        bool selected = (i + 1) == s_selected_rating;
        lv_obj_set_style_bg_color(s_review_rating_btns[i],
                                   lv_color_hex(selected ? COLOR_BTN_ACTIVE : COLOR_BTN), 0);
    }
}

static void show_confirm_discard(bool show)
{
    if (show) {
        lv_obj_add_flag(s_review_breakdown_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_review_rating_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_review_log_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_review_forget_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_review_confirm_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_review_confirm_discard_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_review_confirm_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_review_confirm_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_review_confirm_discard_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_review_confirm_cancel_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_review_breakdown_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_review_rating_row, LV_OBJ_FLAG_HIDDEN);
        if (s_selected_rating > 0) {
            lv_obj_remove_flag(s_review_log_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_review_forget_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void close_review_dialog(void)
{
    lv_obj_add_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);
    s_review_open = false;
}

static void open_review_dialog(void)
{
    char total_text[32];
    format_hms(PracticeSession_GetTotalElapsedSeconds(), total_text, sizeof(total_text));
    char total_label_text[48];
    snprintf(total_label_text, sizeof(total_label_text), "Total: %s", total_text);
    lv_label_set_text(s_review_total_label, total_label_text);

    char breakdown[1024] = "";
    size_t off = 0;
    int count = PracticeSession_GetSegmentCount();
    for (int i = 0; i < count && off + 96 < sizeof(breakdown); i++) {
        char name[GUITAR_NAME_MAX_LEN] = "";
        uint32_t seconds = 0;
        if (!PracticeSession_GetSegment(i, name, sizeof(name), &seconds)) continue;
        char hms[16];
        format_hms(seconds, hms, sizeof(hms));
        off += snprintf(breakdown + off, sizeof(breakdown) - off, "%s%s: %s", i == 0 ? "" : "\n", name, hms);
    }
    lv_label_set_text(s_review_breakdown_label, breakdown);

    s_selected_rating = 0;
    refresh_rating_buttons_highlight();
    lv_obj_add_flag(s_review_log_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_review_forget_btn, LV_OBJ_FLAG_HIDDEN);
    show_confirm_discard(false);

    lv_obj_remove_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);
    s_review_open = true;
}

static void rating_btn_event_cb(lv_event_t *e)
{
    s_selected_rating = (int)(intptr_t)lv_event_get_user_data(e);
    refresh_rating_buttons_highlight();
    lv_obj_remove_flag(s_review_log_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_review_forget_btn, LV_OBJ_FLAG_HIDDEN);
}

static void log_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected_rating < 1) return;
    PracticeSession_Log((uint8_t)s_selected_rating);
    close_review_dialog();
}

static void forget_btn_event_cb(lv_event_t *e)
{
    (void)e;
    show_confirm_discard(true);
}

static void confirm_cancel_event_cb(lv_event_t *e)
{
    (void)e;
    show_confirm_discard(false);
}

static void confirm_discard_event_cb(lv_event_t *e)
{
    (void)e;
    PracticeSession_Discard();
    close_review_dialog();
}

static void build_review_dialog(lv_obj_t *parent)
{
    s_review_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_review_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_review_overlay, 0, 0);
    lv_obj_remove_flag(s_review_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_review_overlay, 0, 0);
    lv_obj_set_style_radius(s_review_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_review_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_review_overlay, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_review_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);

    s_review_total_label = lv_label_create(s_review_overlay);
    lv_obj_set_style_text_font(s_review_total_label, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(s_review_total_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(s_review_total_label, LV_ALIGN_TOP_MID, 0, 50);

    s_review_breakdown_label = lv_label_create(s_review_overlay);
    lv_label_set_text(s_review_breakdown_label, "");
    lv_obj_set_style_text_font(s_review_breakdown_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_review_breakdown_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_review_breakdown_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_review_breakdown_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_review_breakdown_label, 380);
    lv_obj_align(s_review_breakdown_label, LV_ALIGN_TOP_MID, 0, 100);

    // Groups "How did it go?" + the 5 rating buttons + the Unhappy/Happy
    // hint labels so show_confirm_discard can hide/show all of them as one
    // unit. Sized to span the full overlay width so TOP_MID/TOP_LEFT/
    // TOP_RIGHT alignment below behaves the same as if these were direct
    // children of s_review_overlay -- children use local y (their absolute
    // y minus this row's own y) since LVGL aligns relative to the parent.
    const int rating_row_y = 143;
    s_review_rating_row = lv_obj_create(s_review_overlay);
    lv_obj_remove_flag(s_review_rating_row, LV_OBJ_FLAG_SCROLLABLE);
    // Tall enough that the Unhappy/Happy labels' descenders (g/p/y) aren't
    // clipped by the container's own bottom edge -- LVGL clips children to
    // their parent's bounds by default, and this row was too short for that.
    lv_obj_set_size(s_review_rating_row, lv_pct(100), 175);
    lv_obj_set_pos(s_review_rating_row, 0, rating_row_y);
    lv_obj_set_style_bg_opa(s_review_rating_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_review_rating_row, 0, 0);
    // Unlike every other full-width container in this codebase, this one's
    // default (non-zero) padding was never zeroed -- it was shifting all
    // this row's children right by the pad amount, breaking both the
    // buttons' horizontal centering and the +/-start_x symmetry the
    // Unhappy/Happy TOP_LEFT/TOP_RIGHT alignment below depends on.
    lv_obj_set_style_pad_all(s_review_rating_row, 0, 0);

    lv_obj_t *how_label = lv_label_create(s_review_rating_row);
    lv_label_set_text(how_label, "How did it go?");
    lv_obj_set_style_text_font(how_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(how_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(how_label, LV_ALIGN_TOP_MID, 0, 0);

    const int btn_w = 60, btn_h = 54, gap = 8;
    const int row_width = btn_w * 5 + gap * 4;
    const int start_x = SCREEN_CENTER_X - row_width / 2;
    const int btns_y = 35; // below "How did it go?" (local coords within the row)
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_button_create(s_review_rating_row);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, start_x + i * (btn_w + gap), btns_y);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BTN), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, rating_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
        s_review_rating_btns[i] = btn;

        lv_obj_t *lbl = lv_label_create(btn);
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        lv_label_set_text(lbl, num);
        lv_obj_set_style_text_font(lbl, &lv_font_dejavu_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_center(lbl);
    }

    // Explicit gap below the buttons (not an implicit bottom-align against
    // an oversized box) -- an earlier version of this dialog anchored these
    // to the row's bottom edge and they visually crowded/overlapped the
    // buttons on real hardware.
    const int hint_y = btns_y + btn_h + 20;
    lv_obj_t *unhappy_label = lv_label_create(s_review_rating_row);
    lv_label_set_text(unhappy_label, "Unhappy");
    lv_obj_set_style_text_font(unhappy_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(unhappy_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(unhappy_label, LV_ALIGN_TOP_LEFT, start_x, hint_y);

    lv_obj_t *happy_label = lv_label_create(s_review_rating_row);
    lv_label_set_text(happy_label, "Happy");
    lv_obj_set_style_text_font(happy_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(happy_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(happy_label, LV_ALIGN_TOP_RIGHT, -start_x, hint_y);

    s_review_log_btn = lv_button_create(s_review_overlay);
    lv_obj_set_size(s_review_log_btn, 220, 56);
    lv_obj_set_pos(s_review_log_btn, SCREEN_CENTER_X - 110, 316);
    lv_obj_set_style_radius(s_review_log_btn, 12, 0);
    lv_obj_set_style_bg_color(s_review_log_btn, lv_color_hex(COLOR_BTN_START), 0);
    lv_obj_set_style_border_width(s_review_log_btn, 0, 0);
    lv_obj_add_event_cb(s_review_log_btn, log_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *log_lbl = lv_label_create(s_review_log_btn);
    lv_label_set_text(log_lbl, "Log Session");
    lv_obj_set_style_text_font(log_lbl, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(log_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(log_lbl);

    // Kept well clear of the round display's visible-circle boundary --
    // corners this close to the horizontal center stay safely inside it,
    // but this button was previously low enough (y=414) that its bottom
    // corners fell outside the circle and got cropped by the bezel.
    s_review_forget_btn = lv_button_create(s_review_overlay);
    lv_obj_set_size(s_review_forget_btn, 220, 56);
    lv_obj_set_pos(s_review_forget_btn, SCREEN_CENTER_X - 110, 380);
    lv_obj_set_style_radius(s_review_forget_btn, 12, 0);
    lv_obj_set_style_bg_color(s_review_forget_btn, lv_color_hex(COLOR_BTN), 0);
    lv_obj_set_style_border_width(s_review_forget_btn, 0, 0);
    lv_obj_add_event_cb(s_review_forget_btn, forget_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *forget_lbl = lv_label_create(s_review_forget_btn);
    lv_label_set_text(forget_lbl, "Forget Session");
    lv_obj_set_style_text_font(forget_lbl, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(forget_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(forget_lbl);

    s_review_confirm_label = lv_label_create(s_review_overlay);
    lv_label_set_text(s_review_confirm_label, "Discard this practice session?");
    lv_obj_set_style_text_font(s_review_confirm_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_review_confirm_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_review_confirm_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_review_confirm_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_review_confirm_label, 320);
    lv_obj_align(s_review_confirm_label, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_add_flag(s_review_confirm_label, LV_OBJ_FLAG_HIDDEN);

    s_review_confirm_discard_btn = lv_button_create(s_review_overlay);
    lv_obj_set_size(s_review_confirm_discard_btn, 220, 56);
    lv_obj_set_pos(s_review_confirm_discard_btn, SCREEN_CENTER_X - 110, 280);
    lv_obj_set_style_radius(s_review_confirm_discard_btn, 12, 0);
    lv_obj_set_style_bg_color(s_review_confirm_discard_btn, lv_color_hex(COLOR_BTN_STOP), 0);
    lv_obj_set_style_border_width(s_review_confirm_discard_btn, 0, 0);
    lv_obj_add_event_cb(s_review_confirm_discard_btn, confirm_discard_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_review_confirm_discard_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *discard_lbl = lv_label_create(s_review_confirm_discard_btn);
    lv_label_set_text(discard_lbl, "Discard");
    lv_obj_set_style_text_font(discard_lbl, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(discard_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(discard_lbl);

    s_review_confirm_cancel_btn = lv_button_create(s_review_overlay);
    lv_obj_set_size(s_review_confirm_cancel_btn, 220, 56);
    lv_obj_set_pos(s_review_confirm_cancel_btn, SCREEN_CENTER_X - 110, 344);
    lv_obj_set_style_radius(s_review_confirm_cancel_btn, 12, 0);
    lv_obj_set_style_bg_color(s_review_confirm_cancel_btn, lv_color_hex(COLOR_BTN), 0);
    lv_obj_set_style_border_width(s_review_confirm_cancel_btn, 0, 0);
    lv_obj_add_event_cb(s_review_confirm_cancel_btn, confirm_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_review_confirm_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *cancel_lbl = lv_label_create(s_review_confirm_cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(cancel_lbl);
}

// ---------------------------------------------------------------------------
// Card (name, live timer, practice button)
// ---------------------------------------------------------------------------

static void practice_btn_event_cb(lv_event_t *e)
{
    (void)e;
    char id[GUITAR_ID_MAX_LEN] = "";
    char name[GUITAR_NAME_MAX_LEN] = "";
    GuitarCollection_GetId(s_current_index, id, sizeof(id));
    GuitarCollection_GetName(s_current_index, name, sizeof(name));

    switch (PracticeSession_GetState()) {
        case PRACTICE_STATE_IDLE:
            PracticeSession_Start(id, name);
            break;
        case PRACTICE_STATE_ACTIVE:
            if (PracticeSession_IsCurrentGuitar(id)) {
                PracticeSession_End();
                open_review_dialog();
            } else {
                PracticeSession_SwitchGuitar(id, name);
            }
            break;
        default:
            break;
    }
}

void GuitarCollectionUI_Create(lv_obj_t *parent)
{
    // Not pixel-verified on real hardware; nudge if the bezel or overlap
    // looks off once flashed, same caveat as the rest of this UI. (Used to
    // be sized to squeeze in above PairingUI's action button when both
    // shared this tile -- that UI has since moved to the Settings tile, so
    // this geometry is no longer load-bearing, just untouched.)
    //
    // Text-only for now -- guitar photos (fetch + JPEG decode + display)
    // caused persistent, hard-to-pin-down visible flicker on real hardware
    // (a real ~2.5s-per-fetch TLS handshake cost got fixed via connection
    // reuse, and a blank-flash-on-every-swipe UX issue got fixed too, but
    // flicker persisted beyond both fixes) and were pulled entirely rather
    // than keep chasing it. The Guitar Vault thumbnail endpoint is left in
    // place server-side if this gets revisited later.
    //
    // This used to inherit its dark background as a side effect of
    // WiFiProvisioningUI_Create() styling the same shared parent tile --
    // that UI has since moved to the Settings tile, so this tile needs to
    // set its own background now.
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    s_root = lv_obj_create(parent);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_name_label = lv_label_create(s_root);
    lv_label_set_text(s_name_label, "");
    lv_obj_set_style_text_font(s_name_label, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(s_name_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_name_label, 380);
    lv_obj_align(s_name_label, LV_ALIGN_CENTER, 0, -125);

    s_timer_label = lv_label_create(s_root);
    lv_label_set_text(s_timer_label, "");
    lv_obj_set_style_text_font(s_timer_label, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(s_timer_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(s_timer_label, LV_ALIGN_CENTER, 0, -35);
    lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);

    s_practice_btn = lv_button_create(s_root);
    lv_obj_set_size(s_practice_btn, 260, 72);
    lv_obj_align(s_practice_btn, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_style_radius(s_practice_btn, 12, 0);
    lv_obj_set_style_bg_color(s_practice_btn, lv_color_hex(COLOR_BTN_START), 0);
    lv_obj_set_style_border_width(s_practice_btn, 0, 0);
    lv_obj_add_event_cb(s_practice_btn, practice_btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_practice_btn_label = lv_label_create(s_practice_btn);
    lv_label_set_text(s_practice_btn_label, "Start Practice");
    lv_obj_set_style_text_font(s_practice_btn_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_practice_btn_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(s_practice_btn_label);

    s_hint_label = lv_label_create(parent);
    lv_label_set_text(s_hint_label, "Set up Wi-Fi & Guitar Vault in Settings");
    lv_obj_set_style_text_font(s_hint_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint_label, 320);
    lv_obj_center(s_hint_label);

    build_review_dialog(parent);
}

static void refresh_practice_button(void)
{
    if (s_review_open) {
        // Dialog covers everything -- nothing to refresh underneath it.
        return;
    }

    char id[GUITAR_ID_MAX_LEN] = "";
    GuitarCollection_GetId(s_current_index, id, sizeof(id));

    card_btn_kind_t kind;
    practice_state_t state = PracticeSession_GetState();
    if (state == PRACTICE_STATE_ACTIVE) {
        kind = PracticeSession_IsCurrentGuitar(id) ? CARD_BTN_END : CARD_BTN_SWITCH;
    } else {
        kind = CARD_BTN_START;
    }

    if (kind != s_last_rendered_btn_kind) {
        s_last_rendered_btn_kind = kind;
        switch (kind) {
            case CARD_BTN_START:
                lv_label_set_text(s_practice_btn_label, "Start Practice");
                lv_obj_set_style_bg_color(s_practice_btn, lv_color_hex(COLOR_BTN_START), 0);
                break;
            case CARD_BTN_END:
                lv_label_set_text(s_practice_btn_label, "End Practice");
                lv_obj_set_style_bg_color(s_practice_btn, lv_color_hex(COLOR_BTN_STOP), 0);
                break;
            case CARD_BTN_SWITCH:
                lv_label_set_text(s_practice_btn_label, "Switch Guitar");
                lv_obj_set_style_bg_color(s_practice_btn, lv_color_hex(COLOR_ACCENT_OTHER), 0);
                break;
            default:
                break;
        }
    }

    if (state == PRACTICE_STATE_ACTIVE) {
        uint32_t seconds = PracticeSession_GetTotalElapsedSeconds();
        if (seconds != s_last_rendered_seconds) {
            s_last_rendered_seconds = seconds;
            char hms[16];
            format_hms(seconds, hms, sizeof(hms));
            lv_label_set_text(s_timer_label, hms);
        }
        lv_obj_remove_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);
        s_last_rendered_seconds = UINT32_MAX;
    }
}

void GuitarCollectionUI_Tick(void)
{
    bool active = is_active();

    if (active && !s_was_active) {
        // Just became paired AND connected -- kick off the first load.
        s_current_index = 0;
        s_ticks_since_last_attempt = 0;
        s_refresh_attempts = 0;
        GuitarCollection_Refresh();
    }
    s_was_active = active;

    if (!active) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);

    if (!GuitarCollection_IsLoaded()) {
        // Retry periodically (slowly -- each attempt is a real HTTPS+TLS
        // round trip) until a refresh succeeds, capped so a persistently
        // broken account/network doesn't retry forever.
        if (s_refresh_attempts < MAX_AUTO_RETRIES && ++s_ticks_since_last_attempt >= RETRY_INTERVAL_TICKS) {
            s_ticks_since_last_attempt = 0;
            s_refresh_attempts++;
            GuitarCollection_Refresh();
        }
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (GuitarCollection_Count() == 0) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    // Practice button / live timer need re-evaluating every tick regardless
    // of whether the displayed card changed -- a tap can change
    // PracticeSession's state without changing s_current_index, and the
    // timer climbs continuously while a session is active.
    refresh_practice_button();

    // Review dialog opens synchronously from the practice button's own tap
    // handler (see practice_btn_event_cb), not detected here -- but closing
    // it (Log/Discard) is also synchronous, so nothing else to poll for.

    if (s_current_index != s_last_rendered_index) {
        s_last_rendered_index = s_current_index;
        char name[GUITAR_NAME_MAX_LEN] = "";
        GuitarCollection_GetName(s_current_index, name, sizeof(name));
        lv_label_set_text(s_name_label, name);
    }
}

bool GuitarCollectionUI_IsActive(void)
{
    return is_active() && GuitarCollection_IsLoaded() && GuitarCollection_Count() > 0;
}

bool GuitarCollectionUI_IsModalOpen(void)
{
    return s_review_open;
}

void GuitarCollectionUI_HandleSwipe(bool swipe_left)
{
    int count = GuitarCollection_Count();
    if (count == 0) return;

    if (swipe_left) {
        s_current_index = (s_current_index + 1) % count;
    } else {
        s_current_index = (s_current_index - 1 + count) % count;
    }
}
