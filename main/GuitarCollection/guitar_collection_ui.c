#include "guitar_collection_ui.h"
#include "guitar_collection.h"
#include "pairing.h"
#include "wifi_provisioning.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_dejavu_18);

#define COLOR_BG   0x101418
#define COLOR_TEXT 0xE8E8E8

static lv_obj_t *s_root;
static lv_obj_t *s_name_label;
static lv_obj_t *s_hint_label;

static int s_current_index = 0;
static bool s_was_active = false;
static uint32_t s_ticks_since_last_attempt = 0;
static int s_refresh_attempts = 0;
static int s_last_rendered_index = -1;

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
    lv_obj_set_size(s_root, 340, 150);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, -5);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_name_label = lv_label_create(s_root);
    lv_label_set_text(s_name_label, "");
    lv_obj_set_style_text_font(s_name_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_name_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_name_label, 320);
    lv_obj_center(s_name_label);

    s_hint_label = lv_label_create(parent);
    lv_label_set_text(s_hint_label, "Set up Wi-Fi & Guitar Vault in Settings");
    lv_obj_set_style_text_font(s_hint_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint_label, 320);
    lv_obj_center(s_hint_label);
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

    if (s_current_index == s_last_rendered_index) {
        return; // nothing changed
    }
    s_last_rendered_index = s_current_index;

    char name[GUITAR_NAME_MAX_LEN] = "";
    GuitarCollection_GetName(s_current_index, name, sizeof(name));
    lv_label_set_text(s_name_label, name);
}

bool GuitarCollectionUI_IsActive(void)
{
    return is_active() && GuitarCollection_IsLoaded() && GuitarCollection_Count() > 0;
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
