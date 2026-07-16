#include "practice_session.h"
#include "guitar_collection.h" // GUITAR_ID_MAX_LEN, GUITAR_NAME_MAX_LEN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "cJSON.h"

static const char *TAG = "practice_session";

#define GUITAR_VAULT_BASE_URL "https://guitarvault.io"
#define NVS_NAMESPACE "guitar_vault" // shared convention with Pairing/GuitarCollection

#define MAX_SEGMENTS 16 // distinct guitars touched in one session -- generous, trivial RAM cost

typedef struct {
    char id[GUITAR_ID_MAX_LEN];
    char name[GUITAR_NAME_MAX_LEN];
    uint32_t seconds;
} practice_segment_t;

static practice_state_t s_state = PRACTICE_STATE_IDLE;
static practice_segment_t s_segments[MAX_SEGMENTS];
static int s_segment_count;

static char s_current_id[GUITAR_ID_MAX_LEN];
static char s_current_name[GUITAR_NAME_MAX_LEN];
static int64_t s_current_segment_start_us;

static practice_segment_t *find_or_add_segment(const char *id, const char *name)
{
    for (int i = 0; i < s_segment_count; i++) {
        if (strcmp(s_segments[i].id, id) == 0) return &s_segments[i];
    }
    if (s_segment_count >= MAX_SEGMENTS) return NULL; // drop time for a guitar past the cap
    practice_segment_t *seg = &s_segments[s_segment_count++];
    strlcpy(seg->id, id, sizeof(seg->id));
    strlcpy(seg->name, name, sizeof(seg->name));
    seg->seconds = 0;
    return seg;
}

static void finalize_current_segment(void)
{
    int64_t elapsed_us = esp_timer_get_time() - s_current_segment_start_us;
    uint32_t elapsed_s = (uint32_t)(elapsed_us / 1000000);
    practice_segment_t *seg = find_or_add_segment(s_current_id, s_current_name);
    if (seg) seg->seconds += elapsed_s;
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
// Public API
// ---------------------------------------------------------------------------

void PracticeSession_Init(void)
{
    s_state = PRACTICE_STATE_IDLE;
    s_segment_count = 0;
}

practice_state_t PracticeSession_GetState(void)
{
    return s_state;
}

void PracticeSession_Start(const char *guitar_id, const char *guitar_name)
{
    if (s_state != PRACTICE_STATE_IDLE) return;
    s_segment_count = 0;
    strlcpy(s_current_id, guitar_id, sizeof(s_current_id));
    strlcpy(s_current_name, guitar_name, sizeof(s_current_name));
    s_current_segment_start_us = esp_timer_get_time();
    s_state = PRACTICE_STATE_ACTIVE;
}

bool PracticeSession_IsCurrentGuitar(const char *guitar_id)
{
    return s_state == PRACTICE_STATE_ACTIVE && strcmp(s_current_id, guitar_id) == 0;
}

void PracticeSession_SwitchGuitar(const char *guitar_id, const char *guitar_name)
{
    if (s_state != PRACTICE_STATE_ACTIVE) return;
    if (strcmp(s_current_id, guitar_id) == 0) return;

    finalize_current_segment();
    strlcpy(s_current_id, guitar_id, sizeof(s_current_id));
    strlcpy(s_current_name, guitar_name, sizeof(s_current_name));
    s_current_segment_start_us = esp_timer_get_time();
}

uint32_t PracticeSession_GetTotalElapsedSeconds(void)
{
    uint32_t total = 0;
    for (int i = 0; i < s_segment_count; i++) {
        total += s_segments[i].seconds;
    }
    if (s_state == PRACTICE_STATE_ACTIVE) {
        int64_t elapsed_us = esp_timer_get_time() - s_current_segment_start_us;
        total += (uint32_t)(elapsed_us / 1000000);
    }
    return total;
}

void PracticeSession_End(void)
{
    if (s_state != PRACTICE_STATE_ACTIVE) return;
    finalize_current_segment();
    s_state = PRACTICE_STATE_REVIEW;
}

int PracticeSession_GetSegmentCount(void)
{
    return s_segment_count;
}

bool PracticeSession_GetSegment(int index, char *name_out, size_t name_len, uint32_t *seconds_out)
{
    if (index < 0 || index >= s_segment_count) return false;
    strlcpy(name_out, s_segments[index].name, name_len);
    *seconds_out = s_segments[index].seconds;
    return true;
}

void PracticeSession_Discard(void)
{
    s_segment_count = 0;
    s_state = PRACTICE_STATE_IDLE;
}

// ---------------------------------------------------------------------------
// Logging (background HTTP POST)
// ---------------------------------------------------------------------------

typedef struct {
    practice_segment_t segments[MAX_SEGMENTS];
    int segment_count;
    uint8_t rating;
} log_task_arg_t;

static void log_task(void *arg)
{
    log_task_arg_t *data = (log_task_arg_t *)arg;

    char api_key[128];
    if (!load_api_key(api_key, sizeof(api_key))) {
        ESP_LOGW(TAG, "no stored API key, can't log practice session");
        free(data);
        vTaskDelete(NULL);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *segments_json = cJSON_AddArrayToObject(root, "segments");
    for (int i = 0; i < data->segment_count; i++) {
        cJSON *seg = cJSON_CreateObject();
        cJSON_AddStringToObject(seg, "guitarId", data->segments[i].id);
        cJSON_AddNumberToObject(seg, "durationSeconds", data->segments[i].seconds);
        cJSON_AddItemToArray(segments_json, seg);
    }
    cJSON_AddNumberToObject(root, "rating", data->rating);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(data);

    if (!body) {
        ESP_LOGE(TAG, "failed to serialize practice session body");
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = GUITAR_VAULT_BASE_URL "/api/practice-sessions",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client) {
        char auth_header[160];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
        esp_http_client_set_header(client, "Authorization", auth_header);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));

        esp_err_t err = esp_http_client_perform(client);
        int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
        if (status != 201) {
            ESP_LOGW(TAG, "practice session log failed, status %d", status);
        } else {
            ESP_LOGI(TAG, "practice session logged");
        }
        esp_http_client_cleanup(client);
    }
    free(body);
    vTaskDelete(NULL);
}

void PracticeSession_Log(uint8_t rating)
{
    if (s_state != PRACTICE_STATE_REVIEW) return;

    log_task_arg_t *data = malloc(sizeof(log_task_arg_t));
    if (!data) {
        PracticeSession_Discard();
        return;
    }
    memcpy(data->segments, s_segments, sizeof(s_segments));
    data->segment_count = s_segment_count;
    data->rating = rating;

    // Return to IDLE immediately -- the UI closes the dialog right away, and
    // the POST happens silently in the background (see practice_session.h:
    // no retry flow here, matches Pairing/GuitarCollection's "best effort"
    // treatment of background network calls).
    s_segment_count = 0;
    s_state = PRACTICE_STATE_IDLE;

    // Pinned to core 1 -- see wifi_provisioning.c's WiFiProvisioning_Init for
    // why (equal priority on the main loop's own core still means shared
    // time-slicing, not true parallelism).
    xTaskCreatePinnedToCore(log_task, "practice_log", 6144, data, tskIDLE_PRIORITY + 1, NULL, 1);
}
