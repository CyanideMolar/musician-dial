#include "wifi_provisioning_ui.h"
#include "wifi_provisioning.h"

#include <stdio.h>

LV_FONT_DECLARE(lv_font_dejavu_18);
LV_FONT_DECLARE(lv_font_dejavu_32);

#define SCREEN_CENTER_X 240

// Same palette as metronome_ui.c -- kept in sync by eye, not shared code,
// matching how circle_of_fifths_ui.c also duplicates rather than imports it.
#define COLOR_BG         0x101418
#define COLOR_TEXT       0xE8E8E8
#define COLOR_BTN_START  0x2E7D32
#define COLOR_BTN        0x1F252C

static lv_obj_t *s_status_label;
static lv_obj_t *s_setup_btn;
static lv_obj_t *s_setup_btn_label;
static lv_obj_t *s_ap_info_label;

static wifi_prov_state_t s_last_rendered_state = (wifi_prov_state_t)-1;

// Same state-driven single-button pattern as pairing_ui.c's action button --
// label/color/action decided by current state at click time.
static void setup_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (WiFiProvisioning_GetState() == WIFI_PROV_STATE_CONNECTED) {
        WiFiProvisioning_Forget();
    } else {
        WiFiProvisioning_StartSetup();
    }
}

void WiFiProvisioningUI_Create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_font(title, &lv_font_dejavu_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 110);

    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_font(s_status_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_label, 320);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, -20);

    s_ap_info_label = lv_label_create(parent);
    lv_label_set_text(s_ap_info_label, "");
    lv_obj_set_style_text_font(s_ap_info_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_ap_info_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(s_ap_info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ap_info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ap_info_label, 320);
    lv_obj_align(s_ap_info_label, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(s_ap_info_label, LV_OBJ_FLAG_HIDDEN);

    s_setup_btn = lv_obj_create(parent);
    lv_obj_remove_flag(s_setup_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_setup_btn, 220, 60);
    lv_obj_align(s_setup_btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_radius(s_setup_btn, 12, 0);
    lv_obj_set_style_bg_color(s_setup_btn, lv_color_hex(COLOR_BTN_START), 0);
    lv_obj_add_event_cb(s_setup_btn, setup_btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_setup_btn_label = lv_label_create(s_setup_btn);
    lv_label_set_text(s_setup_btn_label, "Set up Wi-Fi");
    lv_obj_set_style_text_font(s_setup_btn_label, &lv_font_dejavu_18, 0);
    lv_obj_set_style_text_color(s_setup_btn_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(s_setup_btn_label);
}

void WiFiProvisioningUI_Tick(void)
{
    wifi_prov_state_t state = WiFiProvisioning_GetState();
    if (state == s_last_rendered_state) return;
    s_last_rendered_state = state;

    lv_obj_add_flag(s_setup_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ap_info_label, LV_OBJ_FLAG_HIDDEN);

    switch (state) {
        case WIFI_PROV_STATE_CONNECTING:
            lv_label_set_text(s_status_label, "Connecting to Wi-Fi...");
            break;

        case WIFI_PROV_STATE_AP_MODE: {
            char ssid[33], pass[9];
            WiFiProvisioning_GetApCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
            lv_label_set_text(s_status_label, "Join this Wi-Fi network from your phone,\nthen open 192.168.4.1 in a browser:");
            char info[96];
            snprintf(info, sizeof(info), "Network: %s\nPassword: %s", ssid, pass);
            lv_label_set_text(s_ap_info_label, info);
            lv_obj_remove_flag(s_ap_info_label, LV_OBJ_FLAG_HIDDEN);
            break;
        }

        case WIFI_PROV_STATE_CONNECTED: {
            char status[64];
            snprintf(status, sizeof(status), "Connected to %s", WiFiProvisioning_GetConnectedSsid());
            lv_label_set_text(s_status_label, status);
            lv_label_set_text(s_setup_btn_label, "Forget");
            lv_obj_set_style_bg_color(s_setup_btn, lv_color_hex(COLOR_BTN), 0);
            lv_obj_remove_flag(s_setup_btn, LV_OBJ_FLAG_HIDDEN);
            break;
        }

        case WIFI_PROV_STATE_FAILED:
        default:
            lv_label_set_text(s_status_label, "Not connected to Wi-Fi");
            lv_label_set_text(s_setup_btn_label, "Set up Wi-Fi");
            lv_obj_set_style_bg_color(s_setup_btn, lv_color_hex(COLOR_BTN_START), 0);
            lv_obj_remove_flag(s_setup_btn, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}
