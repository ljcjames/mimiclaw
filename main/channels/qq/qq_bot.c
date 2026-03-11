#include "qq_bot.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_timer.h"

static const char *TAG = "qq";

/* ── QQ Bot API endpoints ─────────────────────────────────── */
#define QQ_API_BASE         "https://api.sgroup.qq.com"
#define QQ_SEND_MSG_URL     QQ_API_BASE "/v2/groups/%s/messages"
#define QQ_GET_GROUP_ID_URL QQ_API_BASE "/v2/users/@me/groups"

/* ── Credentials & state ───────────────────────────────────── */
static char s_app_id[64] = "";
static char s_token[256] = "";
static int64_t s_token_expire_time = 0;

/* ── Message deduplication ─────────────────────────────────── */
#define QQ_DEDUP_CACHE_SIZE 64

static uint64_t s_seen_msg_keys[QQ_DEDUP_CACHE_SIZE] = {0};
static size_t s_seen_msg_idx = 0;

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    if (!s) return h;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static bool dedup_check_and_record(const char *message_id)
{
    uint64_t key = fnv1a64(message_id);
    for (size_t i = 0; i < QQ_DEDUP_CACHE_SIZE; i++) {
        if (s_seen_msg_keys[i] == key) return true;
    }
    s_seen_msg_keys[s_seen_msg_idx] = key;
    s_seen_msg_idx = (s_seen_msg_idx + 1) % QQ_DEDUP_CACHE_SIZE;
    return false;
}

/* ── HTTP response accumulator ─────────────────────────────── */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_resp_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->len + evt->data_len >= resp->cap) {
            size_t new_cap = resp->cap * 2;
            if (new_cap < resp->len + evt->data_len + 1) {
                new_cap = resp->len + evt->data_len + 1;
            }
            char *tmp = realloc(resp->buf, new_cap);
            if (!tmp) return ESP_ERR_NO_MEM;
            resp->buf = tmp;
            resp->cap = new_cap;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

/* ── Get / refresh access token ─────────────────────────────── */
static esp_err_t qq_get_token(void)
{
    if (s_app_id[0] == '\0' || s_token[0] == '\0') {
        ESP_LOGW(TAG, "No QQ bot credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now = esp_timer_get_time() / 1000000LL;
    if (s_token_expire_time > now + 300) {
        return ESP_OK;
    }

    /* QQ bot token is static, no refresh needed */
    return ESP_OK;
}

/* ── QQ API call helper ────────────────────────────────────── */
static char *qq_api_call(const char *url, const char *method, const char *post_data)
{
    if (qq_get_token() != ESP_OK) return NULL;

    http_resp_t resp = { .buf = calloc(1, 4096), .len = 0, .cap = 4096 };
    if (!resp.buf) return NULL;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(resp.buf); return NULL; }

    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", s_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        if (post_data) {
            esp_http_client_set_post_field(client, post_data, strlen(post_data));
        }
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "API call failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }

    return resp.buf;
}

/* ── Process incoming messages ──────────────────────────────── */
__attribute__((unused))
static void process_message(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, data) {
        cJSON *message_id = cJSON_GetObjectItem(item, "message_id");
        cJSON *content = cJSON_GetObjectItem(item, "content");
        cJSON *sender = cJSON_GetObjectItem(item, "sender");
        cJSON *sender_id = cJSON_GetObjectItem(sender, "user_id");
        cJSON *group_id = cJSON_GetObjectItem(item, "group_id");

        if (!message_id || !cJSON_IsString(message_id)) continue;
        if (!content || !cJSON_IsString(content)) continue;

        const char *msg_id = message_id->valuestring;
        const char *text = content->valuestring;
        const char *user_id = sender_id ? sender_id->valuestring : "";
        const char *gid = group_id ? group_id->valuestring : "";

        /* Deduplication */
        if (dedup_check_and_record(msg_id)) {
            ESP_LOGD(TAG, "Duplicate message %s, skipping", msg_id);
            continue;
        }

        ESP_LOGI(TAG, "Message from user %s in group %s: %.60s%s",
                 user_id, gid, text, strlen(text) > 60 ? "..." : "");

        /* Push to inbound message bus */
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_QQ, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, gid, sizeof(msg.chat_id) - 1);
        msg.content = strdup(text);

        if (msg.content) {
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                ESP_LOGW(TAG, "Inbound queue full, dropping qq message");
                free(msg.content);
            }
        }
    }

    cJSON_Delete(root);
}

/* ── QQ webhook event handler ───────────────────────────────── */
static void qq_webhook_task(void *arg)
{
    (void)arg;

    /* QQ bot uses webhook mode with HTTP server */
    ESP_LOGI(TAG, "QQ webhook server starting");

    /* TODO: Implement HTTP server for webhook events */
    /* For now, just log that webhook is ready */
    ESP_LOGI(TAG, "QQ webhook ready - configure webhook URL in QQ bot settings");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ── Public API ────────────────────────────────────────────── */

esp_err_t qq_bot_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_QQ, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp_id[64] = {0};
        char tmp_token[256] = {0};
        size_t len_id = sizeof(tmp_id);
        size_t len_token = sizeof(tmp_token);

        if (nvs_get_str(nvs, MIMI_NVS_KEY_QQ_APP_ID, tmp_id, &len_id) == ESP_OK && tmp_id[0]) {
            strncpy(s_app_id, tmp_id, sizeof(s_app_id) - 1);
        }
        if (nvs_get_str(nvs, MIMI_NVS_KEY_QQ_TOKEN, tmp_token, &len_token) == ESP_OK && tmp_token[0]) {
            strncpy(s_token, tmp_token, sizeof(s_token) - 1);
        }
        nvs_close(nvs);
    }

    if (s_app_id[0] && s_token[0]) {
        ESP_LOGI(TAG, "QQ bot credentials loaded (app_id=%.8s...)", s_app_id);
    } else {
        ESP_LOGW(TAG, "No QQ bot credentials. Use CLI: set_qq_creds <APP_ID> <TOKEN>");
    }

    return ESP_OK;
}

esp_err_t qq_bot_start(void)
{
    if (s_app_id[0] == '\0' || s_token[0] == '\0') {
        ESP_LOGW(TAG, "QQ bot not configured, skipping webhook start");
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        qq_webhook_task,
        "qq_webhook",
        4096,
        NULL,
        5,
        NULL,
        0);

    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "QQ webhook mode enabled");
    return ESP_OK;
}

esp_err_t qq_send_message(const char *chat_id, const char *text)
{
    if (s_app_id[0] == '\0' || s_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send: no credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    /* QQ bot sends messages to groups */
    char url[256];
    snprintf(url, sizeof(url), QQ_SEND_MSG_URL, chat_id);

    size_t text_len = strlen(text);
    size_t offset = 0;
    int all_ok = 1;

    while (offset < text_len) {
        size_t chunk = text_len - offset;
        if (chunk > 4096) {
            chunk = 4096;
        }

        char *segment = malloc(chunk + 1);
        if (!segment) return ESP_ERR_NO_MEM;
        memcpy(segment, text + offset, chunk);
        segment[chunk] = '\0';

        /* Build message body */
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "content", segment);
        char *json_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        free(segment);

        if (!json_str) {
            offset += chunk;
            all_ok = 0;
            continue;
        }

        ESP_LOGI(TAG, "Sending QQ message to group %s (%d bytes)", chat_id, (int)chunk);
        char *resp = qq_api_call(url, "POST", json_str);
        free(json_str);

        if (resp) {
            cJSON *root = cJSON_Parse(resp);
            if (root) {
                cJSON *code = cJSON_GetObjectItem(root, "code");
                if (code && code->valueint != 0) {
                    cJSON *msg = cJSON_GetObjectItem(root, "msg");
                    ESP_LOGW(TAG, "Send failed: code=%d, msg=%s",
                             code->valueint, msg ? msg->valuestring : "unknown");
                    all_ok = 0;
                } else {
                    ESP_LOGI(TAG, "Sent to group %s (%d bytes)", chat_id, (int)chunk);
                }
                cJSON_Delete(root);
            }
            free(resp);
        } else {
            ESP_LOGE(TAG, "Failed to send message chunk");
            all_ok = 0;
        }

        offset += chunk;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t qq_set_credentials(const char *app_id, const char *token)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_QQ, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_QQ_APP_ID, app_id));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_QQ_TOKEN, token));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_app_id, app_id, sizeof(s_app_id) - 1);
    strncpy(s_token, token, sizeof(s_token) - 1);

    ESP_LOGI(TAG, "QQ bot credentials saved");
    return ESP_OK;
}
