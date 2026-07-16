#include "wifi_provisioning.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_prov";

#define NVS_NAMESPACE   "wifi_cfg"
#define CONNECT_TIMEOUT_TICKS pdMS_TO_TICKS(10000)

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static volatile wifi_prov_state_t s_state = WIFI_PROV_STATE_FAILED;
static EventGroupHandle_t s_wifi_event_group;
static httpd_handle_t s_httpd = NULL;

static char s_ap_ssid[33];
static char s_ap_pass[9];
static char s_connected_ssid[33];

// ---------------------------------------------------------------------------
// Small helpers (NVS, password generation, HTML/URL encoding)
// ---------------------------------------------------------------------------

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    size_t slen = ssid_len, plen = pass_len;
    esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &slen);
    esp_err_t e2 = nvs_get_str(h, "pass", pass, &plen);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && slen > 1;
}

static void save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

static void clear_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "pass");
    nvs_commit(h);
    nvs_close(h);
}

static void generate_random_password(char *out, size_t len)
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    size_t n = len - 1;
    for (size_t i = 0; i < n; i++) {
        out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    out[n] = 0;
}

// Scanned SSIDs are attacker-controlled strings reflected straight into the
// setup page's HTML -- escape before embedding, both as text and inside the
// double-quoted <option value="..."> attribute.
static void html_escape(const char *src, char *dst, size_t dst_len)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 6 < dst_len; si++) {
        unsigned char c = (unsigned char)src[si];
        switch (c) {
            case '<':  di += snprintf(dst + di, dst_len - di, "&lt;"); break;
            case '>':  di += snprintf(dst + di, dst_len - di, "&gt;"); break;
            case '&':  di += snprintf(dst + di, dst_len - di, "&amp;"); break;
            case '"':  di += snprintf(dst + di, dst_len - di, "&quot;"); break;
            case '\'': di += snprintf(dst + di, dst_len - di, "&#39;"); break;
            default:   dst[di++] = (char)c; break;
        }
    }
    dst[di] = '\0';
}

// application/x-www-form-urlencoded decoding ('+' -> space, %XX -> byte).
static void url_decode(const char *src, char *dst, size_t dst_len)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_len; si++) {
        char c = src[si];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && isxdigit((unsigned char)src[si + 1]) && isxdigit((unsigned char)src[si + 2])) {
            char hex[3] = { src[si + 1], src[si + 2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

// ---------------------------------------------------------------------------
// WiFi connection
// ---------------------------------------------------------------------------

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Assumes the STA interface is already started (either WIFI_MODE_STA at boot,
// or WIFI_MODE_APSTA during setup) -- just (re)configures and connects it.
static bool sta_connect_and_wait(const char *ssid, const char *pass, TickType_t timeout)
{
    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, timeout);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void sta_startup_task(void *arg)
{
    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        s_state = WIFI_PROV_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    s_state = WIFI_PROV_STATE_CONNECTING;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (sta_connect_and_wait(ssid, pass, CONNECT_TIMEOUT_TICKS)) {
        strlcpy(s_connected_ssid, ssid, sizeof(s_connected_ssid));
        s_state = WIFI_PROV_STATE_CONNECTED;
        ESP_LOGI(TAG, "Connected to stored network '%s'", ssid);
    } else {
        s_state = WIFI_PROV_STATE_FAILED;
        ESP_LOGW(TAG, "Could not connect to stored network '%s'", ssid);
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Setup HTTP server
// ---------------------------------------------------------------------------

static esp_err_t root_get_handler(httpd_req_t *req)
{
    esp_wifi_scan_start(NULL, true);
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (ap_count > 0 && records) {
        esp_wifi_scan_get_ap_records(&ap_count, records);
    }

    const size_t cap = 6144;
    char *html = malloc(cap);
    if (!html) {
        free(records);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t off = 0;
    off += snprintf(html + off, cap - off,
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Musician Dial \xCE\xB1 Setup</title></head>"
        "<body style='font-family:sans-serif;max-width:400px;margin:2em auto;padding:0 1em'>"
        "<h2>Connect Musician Dial \xCE\xB1 to Wi-Fi</h2>"
        "<form method=POST action=/connect>"
        "<label>Network</label><br>"
        "<select name=ssid style='width:100%%;padding:8px;font-size:1em'>");

    for (int i = 0; i < ap_count && off + 200 < cap; i++) {
        char escaped[80];
        html_escape((const char *)records[i].ssid, escaped, sizeof(escaped));
        off += snprintf(html + off, cap - off, "<option value=\"%s\">%s</option>", escaped, escaped);
    }
    free(records);

    off += snprintf(html + off, cap - off,
        "</select><br><br>"
        "<label>Password</label><br>"
        "<input type=password name=password style='width:100%%;padding:8px;font-size:1em'><br><br>"
        "<button type=submit style='width:100%%;padding:12px;background:#2E7D32;color:white;"
        "border:none;border-radius:6px;font-size:1em'>Connect</button>"
        "</form></body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, off);
    free(html);
    return ESP_OK;
}

static const char SUCCESS_HTML[] =
    "<!doctype html><html><body style='font-family:sans-serif;max-width:400px;margin:2em auto'>"
    "<h2>Connected!</h2><p>You can close this page and return to your dial.</p></body></html>";
static const char FAILURE_HTML[] =
    "<!doctype html><html><body style='font-family:sans-serif;max-width:400px;margin:2em auto'>"
    "<h2>Couldn&#39;t connect</h2><p>Check the password and try again.</p>"
    "<a href='/'>Back</a></body></html>";

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char buf[256];
    int to_read = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;
    int len = httpd_req_recv(req, buf, to_read);
    if (len <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    char ssid_enc[64] = { 0 };
    char pass_enc[128] = { 0 };
    httpd_query_key_value(buf, "ssid", ssid_enc, sizeof(ssid_enc));
    httpd_query_key_value(buf, "password", pass_enc, sizeof(pass_enc));

    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    url_decode(ssid_enc, ssid, sizeof(ssid));
    url_decode(pass_enc, pass, sizeof(pass));

    bool ok = sta_connect_and_wait(ssid, pass, CONNECT_TIMEOUT_TICKS);
    if (ok) {
        save_credentials(ssid, pass);
        strlcpy(s_connected_ssid, ssid, sizeof(s_connected_ssid));
        s_state = WIFI_PROV_STATE_CONNECTED;
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);
        // Drop the setup AP now that we're on the real network. Done after
        // the response is queued -- httpd keeps serving already-accepted
        // connections/responses fine even as the AP netif goes down.
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    } else {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, FAILURE_HTML, HTTPD_RESP_USE_STRLEN);
        // Stay in AP_MODE so the user can retry from the same page.
    }
    return ESP_OK;
}

static void start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start setup HTTP server");
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &connect);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WiFiProvisioning_Init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_state = WIFI_PROV_STATE_FAILED;
    // Pinned to core 1: the main LVGL loop is pinned to core 0
    // (CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0), and matching this task's
    // priority to main's own (tskIDLE_PRIORITY + 1 == ESP_TASK_MAIN_PRIO)
    // still means sharing a single core via time-slicing, not true
    // parallelism -- confirmed on real hardware as reduced-but-still-visible
    // stutter during boot even after that priority fix. Running on the
    // other core entirely removes the contention instead of just softening it.
    xTaskCreatePinnedToCore(sta_startup_task, "wifi_sta_startup", 4096, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}

void WiFiProvisioning_StartSetup(void)
{
    if (s_state == WIFI_PROV_STATE_AP_MODE) return;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    // Plain ASCII "Alpha", not the literal Greek letter used in display text
    // elsewhere -- broadcast SSIDs are safer kept ASCII-only rather than
    // relying on every phone/OS's WiFi picker rendering UTF-8 correctly.
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "MusicianDialAlpha-%02X%02X", mac[4], mac[5]);
    generate_random_password(s_ap_pass, sizeof(s_ap_pass));

    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_config = { 0 };
    strlcpy((char *)ap_config.ap.ssid, s_ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    strlcpy((char *)ap_config.ap.password, s_ap_pass, sizeof(ap_config.ap.password));
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_http_server();
    s_state = WIFI_PROV_STATE_AP_MODE;
}

wifi_prov_state_t WiFiProvisioning_GetState(void)
{
    return s_state;
}

bool WiFiProvisioning_GetApCredentials(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    if (s_state != WIFI_PROV_STATE_AP_MODE) return false;
    strlcpy(ssid_out, s_ap_ssid, ssid_len);
    strlcpy(pass_out, s_ap_pass, pass_len);
    return true;
}

const char *WiFiProvisioning_GetConnectedSsid(void)
{
    return s_state == WIFI_PROV_STATE_CONNECTED ? s_connected_ssid : "";
}

void WiFiProvisioning_Forget(void)
{
    clear_credentials();
    s_connected_ssid[0] = '\0';
    esp_wifi_disconnect();
    s_state = WIFI_PROV_STATE_FAILED;
}
