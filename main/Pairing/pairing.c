#include "pairing.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "cJSON.h"

static const char *TAG = "pairing";

#define GUITAR_VAULT_BASE_URL "https://guitarvault.io"
#define NVS_NAMESPACE "guitar_vault"

#define POLL_INTERVAL_TICKS pdMS_TO_TICKS(3000)
#define MAX_POLL_ATTEMPTS   200 // ~10 minutes, matching the server's pairing-code TTL

static volatile pairing_state_t s_state = PAIRING_STATE_NOT_PAIRED;
static char s_pending_code[16];
static char s_pending_url[128];

// ---------------------------------------------------------------------------
// HTTP helpers -- small JSON responses only, accumulated into a fixed buffer.
// ---------------------------------------------------------------------------

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        response_buf_t *rb = (response_buf_t *)evt->user_data;
        if (rb && rb->len + evt->data_len < rb->cap) {
            memcpy(rb->buf + rb->len, evt->data, evt->data_len);
            rb->len += evt->data_len;
            rb->buf[rb->len] = '\0';
        }
    }
    return ESP_OK;
}

// Performs the request, returns the HTTP status code (or -1 on transport
// failure). Response body (if any) lands in response_buf, always
// null-terminated.
static int http_request(const char *url, esp_http_client_method_t method,
                         const char *body, char *response_buf, size_t response_cap)
{
    response_buf_t rb = { .buf = response_buf, .cap = response_cap, .len = 0 };
    response_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .event_handler = http_event_handler,
        .user_data = &rb,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;

    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request to %s failed: %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return status;
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

static bool load_api_key(char *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, "api_key", out, &len);
    nvs_close(h);
    return err == ESP_OK && len > 1;
}

static void save_api_key(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "api_key", key);
    nvs_commit(h);
    nvs_close(h);
}

static void clear_api_key(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "api_key");
    nvs_commit(h);
    nvs_close(h);
}

// ---------------------------------------------------------------------------
// Pairing task
// ---------------------------------------------------------------------------

static void pairing_task(void *arg)
{
    char resp[512];

    int status = http_request(GUITAR_VAULT_BASE_URL "/api/pair/init", HTTP_METHOD_POST, "", resp, sizeof(resp));
    if (status != 200 && status != 201) {
        ESP_LOGW(TAG, "pair/init failed, status %d", status);
        s_state = PAIRING_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    const cJSON *code_json = root ? cJSON_GetObjectItem(root, "code") : NULL;
    const cJSON *url_json = root ? cJSON_GetObjectItem(root, "approvalUrl") : NULL;
    if (!cJSON_IsString(code_json) || !cJSON_IsString(url_json)) {
        ESP_LOGW(TAG, "pair/init response missing code/approvalUrl");
        cJSON_Delete(root);
        s_state = PAIRING_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }
    strlcpy(s_pending_code, code_json->valuestring, sizeof(s_pending_code));
    strlcpy(s_pending_url, url_json->valuestring, sizeof(s_pending_url));
    cJSON_Delete(root);

    s_state = PAIRING_STATE_AWAITING_APPROVAL;

    char status_url[192];
    snprintf(status_url, sizeof(status_url), GUITAR_VAULT_BASE_URL "/api/pair/status?code=%s", s_pending_code);

    for (int attempt = 0; attempt < MAX_POLL_ATTEMPTS; attempt++) {
        vTaskDelay(POLL_INTERVAL_TICKS);

        status = http_request(status_url, HTTP_METHOD_GET, NULL, resp, sizeof(resp));
        if (status != 200) {
            continue; // transient network hiccup -- keep polling within the attempt budget
        }

        cJSON *poll_root = cJSON_Parse(resp);
        const cJSON *state_json = poll_root ? cJSON_GetObjectItem(poll_root, "status") : NULL;
        if (!cJSON_IsString(state_json)) {
            cJSON_Delete(poll_root);
            continue;
        }

        if (strcmp(state_json->valuestring, "approved") == 0) {
            const cJSON *key_json = cJSON_GetObjectItem(poll_root, "apiKey");
            if (cJSON_IsString(key_json)) {
                save_api_key(key_json->valuestring);
                http_request(status_url, HTTP_METHOD_DELETE, NULL, resp, sizeof(resp)); // best-effort consume
                s_state = PAIRING_STATE_PAIRED;
            } else {
                s_state = PAIRING_STATE_ERROR;
            }
            cJSON_Delete(poll_root);
            vTaskDelete(NULL);
            return;
        }

        if (strcmp(state_json->valuestring, "expired") == 0) {
            cJSON_Delete(poll_root);
            s_state = PAIRING_STATE_ERROR;
            vTaskDelete(NULL);
            return;
        }

        cJSON_Delete(poll_root); // "pending" -- keep polling
    }

    ESP_LOGW(TAG, "Pairing timed out waiting for approval");
    s_state = PAIRING_STATE_ERROR;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Pairing_Init(void)
{
    char key[128];
    s_state = load_api_key(key, sizeof(key)) ? PAIRING_STATE_PAIRED : PAIRING_STATE_NOT_PAIRED;
}

void Pairing_Start(void)
{
    if (s_state == PAIRING_STATE_PAIRED ||
        s_state == PAIRING_STATE_REQUESTING ||
        s_state == PAIRING_STATE_AWAITING_APPROVAL) {
        return;
    }
    s_state = PAIRING_STATE_REQUESTING;
    // Pinned to core 1 -- see wifi_provisioning.c's WiFiProvisioning_Init for
    // why (equal priority on the main loop's own core still means shared
    // time-slicing, not true parallelism).
    xTaskCreatePinnedToCore(pairing_task, "pairing_task", 6144, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}

void Pairing_Forget(void)
{
    clear_api_key();
    s_state = PAIRING_STATE_NOT_PAIRED;
}

pairing_state_t Pairing_GetState(void)
{
    return s_state;
}

bool Pairing_GetPendingInfo(char *code_out, size_t code_len, char *url_out, size_t url_len)
{
    if (s_state != PAIRING_STATE_AWAITING_APPROVAL) return false;
    strlcpy(code_out, s_pending_code, code_len);
    strlcpy(url_out, s_pending_url, url_len);
    return true;
}
