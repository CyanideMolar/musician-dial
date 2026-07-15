#include "pairing_ui.h"
#include "pairing.h"
#include "wifi_provisioning.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_dejavu_18);
LV_FONT_DECLARE(lv_font_dejavu_32);

#define SCREEN_CENTER_X 240

// Same palette as metronome_ui.c / wifi_provisioning_ui.c.
#define COLOR_TEXT       0xE8E8E8
#define COLOR_BTN_START  0x2E7D32
#define COLOR_BTN        0x1F252C

static lv_obj_t *s_root;      // container for all pairing widgets, shown/hidden as a group
static lv_obj_t *s_status_label;
static lv_obj_t *s_qr;
static lv_obj_t *s_code_label;
static lv_obj_t *s_action_btn;
static lv_obj_t *s_action_btn_label;

static pairing_state_t s_last_rendered_state = (pairing_state_t)-1;
static bool s_last_shown = false;

static void action_btn_event_cb(lv_event_t *e)
{
    (void)e;
    pairing_state_t state = Pairing_GetState();
    if (state == PAIRING_STATE_PAIRED) {
        Pairing_Forget();
    } else {
        Pairing_Start();
    }
}

void PairingUI_Create(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Guitar Vault");
    lv_obj_set_style_text_font(title, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 110);

    s_root = lv_obj_create(parent);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_root, 360, 220);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 20);

    s_status_label = lv_label_create(s_root);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_font(s_status_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_label, 340);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 0);

    s_qr = lv_qrcode_create(s_root);
    lv_qrcode_set_size(s_qr, 120);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);

    s_code_label = lv_label_create(s_root);
    lv_label_set_text(s_code_label, "");
    lv_obj_set_style_text_font(s_code_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_code_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_code_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_code_label, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_add_flag(s_code_label, LV_OBJ_FLAG_HIDDEN);

    s_action_btn = lv_obj_create(s_root);
    lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_action_btn, 220, 50);
    lv_obj_align(s_action_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(s_action_btn, 12, 0);
    lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(COLOR_BTN_START), 0);
    lv_obj_add_event_cb(s_action_btn, action_btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_action_btn_label = lv_label_create(s_action_btn);
    lv_label_set_text(s_action_btn_label, "Connect to Guitar Vault");
    lv_obj_set_style_text_font(s_action_btn_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_action_btn_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(s_action_btn_label);
}

void PairingUI_Tick(void)
{
    bool should_show = WiFiProvisioning_GetState() == WIFI_PROV_STATE_CONNECTED;
    pairing_state_t state = Pairing_GetState();

    if (should_show == s_last_shown && state == s_last_rendered_state) return;
    s_last_shown = should_show;
    s_last_rendered_state = state;

    if (!should_show) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_code_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);

    switch (state) {
        case PAIRING_STATE_NOT_PAIRED:
            lv_label_set_text(s_status_label, "Not connected to Guitar Vault");
            lv_label_set_text(s_action_btn_label, "Connect to Guitar Vault");
            lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(COLOR_BTN_START), 0);
            lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
            break;

        case PAIRING_STATE_REQUESTING:
            lv_label_set_text(s_status_label, "Requesting pairing code...");
            break;

        case PAIRING_STATE_AWAITING_APPROVAL: {
            char code[16], url[128];
            Pairing_GetPendingInfo(code, sizeof(code), url, sizeof(url));
            lv_label_set_text(s_status_label, "Scan with your phone, or visit the link, to approve:");
            lv_qrcode_update(s_qr, url, strlen(url));
            lv_obj_remove_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_code_label, code);
            lv_obj_remove_flag(s_code_label, LV_OBJ_FLAG_HIDDEN);
            break;
        }

        case PAIRING_STATE_PAIRED:
            lv_label_set_text(s_status_label, "Connected to Guitar Vault");
            lv_label_set_text(s_action_btn_label, "Forget");
            lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(COLOR_BTN), 0);
            lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
            break;

        case PAIRING_STATE_ERROR:
        default:
            lv_label_set_text(s_status_label, "Couldn't reach Guitar Vault");
            lv_label_set_text(s_action_btn_label, "Try Again");
            lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(COLOR_BTN_START), 0);
            lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}
