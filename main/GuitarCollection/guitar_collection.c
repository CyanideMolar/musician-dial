#include "guitar_collection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "cJSON.h"

static const char *TAG = "guitar_collection";

#define GUITAR_VAULT_BASE_URL "https://guitarvault.io"
#define NVS_NAMESPACE "guitar_vault" // shared convention with Pairing (see pairing.c)

#define MAX_GUITARS 30
#define LIST_RESPONSE_CAP (16 * 1024)

typedef struct {
    char id[GUITAR_ID_MAX_LEN];
    char name[GUITAR_NAME_MAX_LEN];
} guitar_entry_t;

static guitar_entry_t s_guitars[MAX_GUITARS];
static int s_guitar_count = 0;
static volatile bool s_loaded = false;
static volatile bool s_refreshing = false;

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        response_buf_t *rb = (response_buf_t *)evt->user_data;
        if (rb && rb->len + evt->data_len <= rb->cap) {
            memcpy(rb->buf + rb->len, evt->data, evt->data_len);
            rb->len += evt->data_len;
        }
    }
    return ESP_OK;
}

// A single persistent client, reused across requests instead of tearing down
// and re-establishing the TLS connection each time (a fresh
// esp_http_client_init()+cleanup() per call cost ~2.5s per request -- a full
// TLS handshake every time). Guarded by a mutex in case of future concurrent
// callers.
static esp_http_client_handle_t s_http_client = NULL;
static SemaphoreHandle_t s_http_mutex = NULL;

// Returns HTTP status code (-1 on transport failure). Response bytes land in
// response_buf (NOT null-terminated -- caller tracks length via *out_len).
static int http_get(const char *url, const char *bearer_token, uint8_t *response_buf, size_t response_cap, size_t *out_len)
{
    response_buf_t rb = { .buf = response_buf, .cap = response_cap, .len = 0 };

    xSemaphoreTake(s_http_mutex, portMAX_DELAY);

    if (!s_http_client) {
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .event_handler = http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 8000,
            .keep_alive_enable = true,
        };
        s_http_client = esp_http_client_init(&config);
    } else {
        esp_http_client_set_url(s_http_client, url);
        esp_http_client_set_method(s_http_client, HTTP_METHOD_GET);
    }

    if (!s_http_client) {
        xSemaphoreGive(s_http_mutex);
        return -1;
    }

    esp_http_client_set_user_data(s_http_client, &rb);

    char auth_header[160];
    if (bearer_token) {
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", bearer_token);
        esp_http_client_set_header(s_http_client, "Authorization", auth_header);
    } else {
        esp_http_client_delete_header(s_http_client, "Authorization");
    }

    esp_err_t err = esp_http_client_perform(s_http_client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(s_http_client) : -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
        // Connection may be broken -- drop it so the next call opens fresh
        // rather than repeatedly failing on a dead handle.
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
    }
    *out_len = rb.len;

    xSemaphoreGive(s_http_mutex);
    return status;
}

static bool load_api_key(char *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, "api_key", out, &len);
    nvs_close(h);
    return err == ESP_OK && len > 1;
}

// ---------------------------------------------------------------------------
// List refresh
// ---------------------------------------------------------------------------

static void refresh_task(void *arg)
{
    char api_key[128];
    if (!load_api_key(api_key, sizeof(api_key))) {
        ESP_LOGW(TAG, "no stored API key, can't refresh collection");
        s_refreshing = false;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *resp = heap_caps_malloc(LIST_RESPONSE_CAP, MALLOC_CAP_SPIRAM);
    if (!resp) {
        ESP_LOGE(TAG, "failed to allocate %u-byte list response buffer", LIST_RESPONSE_CAP);
        s_refreshing = false;
        vTaskDelete(NULL);
        return;
    }

    size_t resp_len = 0;
    int status = http_get(GUITAR_VAULT_BASE_URL "/api/guitars", api_key, resp, LIST_RESPONSE_CAP - 1, &resp_len);
    if (status != 200) {
        ESP_LOGW(TAG, "GET /api/guitars failed, status %d", status);
        free(resp);
        s_refreshing = false;
        vTaskDelete(NULL);
        return;
    }
    resp[resp_len] = '\0';

    cJSON *root = cJSON_ParseWithLength((const char *)resp, resp_len);
    free(resp);
    if (!cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "unexpected /api/guitars response shape");
        cJSON_Delete(root);
        s_refreshing = false;
        vTaskDelete(NULL);
        return;
    }

    int n = cJSON_GetArraySize(root);
    if (n > MAX_GUITARS) n = MAX_GUITARS;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        const cJSON *id_json = cJSON_GetObjectItem(item, "id");
        const cJSON *name_json = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(id_json)) strlcpy(s_guitars[i].id, id_json->valuestring, sizeof(s_guitars[i].id));
        if (cJSON_IsString(name_json)) strlcpy(s_guitars[i].name, name_json->valuestring, sizeof(s_guitars[i].name));
    }
    cJSON_Delete(root);

    s_guitar_count = n;
    s_loaded = true;
    s_refreshing = false;
    ESP_LOGI(TAG, "loaded %d guitars", n);
    vTaskDelete(NULL);
}

void GuitarCollection_Init(void)
{
    s_guitar_count = 0;
    s_loaded = false;
    s_refreshing = false;
    s_http_mutex = xSemaphoreCreateMutex();
}

void GuitarCollection_Refresh(void)
{
    if (s_refreshing) return;
    s_refreshing = true;
    // Pinned to core 1: the main LVGL loop is pinned to core 0
    // (CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0); running this on the other core
    // avoids sharing a single core with the render loop via time-slicing.
    xTaskCreatePinnedToCore(refresh_task, "guitar_list_fetch", 6144, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}

bool GuitarCollection_IsLoaded(void)
{
    return s_loaded;
}

int GuitarCollection_Count(void)
{
    return s_guitar_count;
}

bool GuitarCollection_GetName(int index, char *out, size_t out_len)
{
    if (index < 0 || index >= s_guitar_count) return false;
    strlcpy(out, s_guitars[index].name, out_len);
    return true;
}

bool GuitarCollection_GetId(int index, char *out, size_t out_len)
{
    if (index < 0 || index >= s_guitar_count) return false;
    strlcpy(out, s_guitars[index].id, out_len);
    return true;
}
