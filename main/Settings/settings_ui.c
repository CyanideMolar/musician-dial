#include "settings_ui.h"
#include "wifi_provisioning_ui.h"
#include "pairing_ui.h"

LV_FONT_DECLARE(lv_font_dejavu_18);
LV_FONT_DECLARE(lv_font_dejavu_32);

#define COLOR_BG   0x101418
#define COLOR_TEXT 0xE8E8E8

#define SETTINGS_PAGE_LANDING     0
#define SETTINGS_PAGE_WIFI        1
#define SETTINGS_PAGE_GUITAR_VAULT 2
#define SETTINGS_PAGE_COUNT       3

static lv_obj_t *s_pages[SETTINGS_PAGE_COUNT];
static int s_active_page;

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    return page;
}

static void build_landing_page(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 110);

    // Rendered as text (LVGL's built-in Montserrat symbol glyph), not a
    // bitmap -- runtime-fetched/decoded images caused persistent flicker and
    // crashes elsewhere in this firmware (see GuitarCollection) and were
    // pulled entirely. This is a static, well-tested LVGL codepath instead.
    lv_obj_t *gear = lv_label_create(page);
    lv_label_set_text(gear, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gear, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(gear, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(gear, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *hint = lv_label_create(page);
    lv_label_set_text(hint, "Swipe for Wi-Fi & Guitar Vault settings");
    lv_obj_set_style_text_font(hint, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 320);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 80);
}

void SettingsUI_Create(lv_obj_t *parent)
{
    s_pages[SETTINGS_PAGE_LANDING] = make_page(parent);
    s_pages[SETTINGS_PAGE_WIFI] = make_page(parent);
    s_pages[SETTINGS_PAGE_GUITAR_VAULT] = make_page(parent);

    build_landing_page(s_pages[SETTINGS_PAGE_LANDING]);
    WiFiProvisioningUI_Create(s_pages[SETTINGS_PAGE_WIFI]);
    PairingUI_Create(s_pages[SETTINGS_PAGE_GUITAR_VAULT]);

    lv_obj_add_flag(s_pages[SETTINGS_PAGE_WIFI], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pages[SETTINGS_PAGE_GUITAR_VAULT], LV_OBJ_FLAG_HIDDEN);
    s_active_page = SETTINGS_PAGE_LANDING;
}

static void show_page(int page)
{
    if (page == s_active_page) return;
    lv_obj_add_flag(s_pages[s_active_page], LV_OBJ_FLAG_HIDDEN);
    s_active_page = page;
    lv_obj_remove_flag(s_pages[s_active_page], LV_OBJ_FLAG_HIDDEN);
}

void SettingsUI_HandleSwipe(bool swipe_left)
{
    int next = swipe_left ? (s_active_page + 1) % SETTINGS_PAGE_COUNT
                           : (s_active_page - 1 + SETTINGS_PAGE_COUNT) % SETTINGS_PAGE_COUNT;
    show_page(next);
}

void SettingsUI_ResetToLanding(void)
{
    show_page(SETTINGS_PAGE_LANDING);
}
