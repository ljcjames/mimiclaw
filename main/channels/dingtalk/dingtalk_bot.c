#include "dingtalk_bot.h"
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

static const char *TAG = "dingtalk";

/* ── DingTalk API endpoints ─────────────────────────────────── */
#define DINGTALK_API_BASE         "https://oapi.dingtalk.com"
#define DINGTALK_GET_TOKEN_URL    DINGTALK_API_BASE "/gettoken"
#define DINGTALK_SEND_MSG_URL     DINGTALK_API_BASE "/robot/send?access_token=%s"

/* ── Credentials & token state ─────────────────────────────── */
static char s_app_key[64] = "";
static char s_app_secret[128] = "";
static char s_access_token[512] = "";
static int64_t s_token_expire_time = 0;

/* ── Message deduplication ─────────────────────────────────── */
#define DINGTALK_DEDUP_CACHE_SIZE 64

static uint64_t s_seen_msg_keys[DINGTALK_DEDUP_CACHE_SIZE] = {0};
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
    for (size_t i = 0; i < DINGTALK_DEDUP_CACHE_SIZE; i++) {
        if (s_seen_msg_keys[i] == key) return true;
    }
    s_seen_msg_keys[s_seen_msg_idx] = key;
    s_seen_msg_idx = (s_seen_msg_idx + 1) % DINGTALK_DEDUP_CACHE_SIZE;
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
static esp_err_t dingtalk_get_token(void)
{
    if (s_app_key[0] == '\0' || s_app_secret[0] == '\0') {
        ESP_LOGW(TAG, "No DingTalk bot credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now = esp_timer_get_time() / 1000000LL;
    if (s_access_token[0] != '\0' && s_token_expire_time > now + 300) {
        return ESP_OK;
    }

    /* Get new token */
    char url[1024];
    snprintf(url, sizeof(url), "%s?appkey=%s&appsecret=%s",
             DINGTALK_GET_TOKEN_URL, s_app_key, s_app_secret);

    http_resp_t resp = { .buf = calloc(1, 4096), .len = 0, .cap = 4096 };
    if (!resp.buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(resp.buf); return ESP_FAIL; }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Token request HTTP failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return err;
    }

    cJSON *root = cJSON_Parse(resp.buf);
    free(resp.buf);
    if (!root) { ESP_LOGE(TAG, "Failed to parse token response"); return ESP_FAIL; }

    cJSON *code = cJSON_GetObjectItem(root, "errcode");
    cJSON *errmsg = cJSON_GetObjectItem(root, "errmsg");
    cJSON *access_token = cJSON_GetObjectItem(root, "access_token");
    cJSON *expire = cJSON_GetObjectItem(root, "expire");

    if (code && cJSON_IsNumber(code) && code->valueint == 0 && access_token && cJSON_IsString(access_token)) {
        strncpy(s_access_token, access_token->valuestring, sizeof(s_access_token) - 1);
        s_token_expire_time = now + (expire ? expire->valueint : 7200) - 300;
        ESP_LOGI(TAG, "Got DingTalk access token (expires in %ds)",
                 expire ? expire->valueint : 7200);
    } else {
        ESP_LOGE(TAG, "Token request failed: errcode=%d, errmsg=%s",
                 code ? code->valueint : -1, errmsg ? errmsg->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── DingTalk API call helper ────────────────────────────────── */
static char *dingtalk_api_call(const char *url, const char *method, const char *post_data)
{
    if (dingtalk_get_token() != ESP_OK) return NULL;

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

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Charset", "UTF-8");

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
static void process_message(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *errcode = cJSON_GetObjectItem(root, "errcode");
    if (errcode && cJSON_IsNumber(errcode) && errcode->valueint == 0) {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (result && cJSON_IsObject(result)) {
            cJSON *message_id = cJSON_GetObjectItem(result, "messageId");
            if (message_id && cJSON_IsString(message_id)) {
                const char *msg_id = message_id->valuestring;

                /* Deduplication */
                if (dedup_check_and_record(msg_id)) {
                    ESP_LOGD(TAG, "Duplicate message %s, skipping", msg_id);
                    cJSON_Delete(root);
                    return;
                }

                ESP_LOGI(TAG, "Message received (message_id=%s)", msg_id);
                /* TODO: Parse message content and push to inbound bus */
            }
        }
    } else {
        cJSON *errmsg = cJSON_GetObjectItem(root, "errmsg");
        ESP_LOGW(TAG, "Message processing failed: %s",
                 errmsg ? errmsg->valuestring : "unknown");
    }

    cJSON_Delete(root);
}

/* ── DingTalk webhook event handler ─────────────────────────── */
static void dingtalk_webhook_task(void *arg)
{
    (void)arg;

    /* DingTalk bot uses webhook mode with HTTP server */
    ESP_LOGI(TAG, "DingTalk webhook server starting");

    /* TODO: Implement HTTP server for webhook events */
    /* For now, just log that webhook is ready */
    ESP_LOGI(TAG, "DingTalk webhook ready - configure webhook URL in DingTalk bot settings");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ── Public API ────────────────────────────────────────────── */

esp_err_t dingtalk_bot_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_DINGTALK, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp_key[64] = {0};
        char tmp_secret[128] = {0};
        size_t len_key = sizeof(tmp_key);
        size_t len_secret = sizeof(tmp_secret);

        if (nvs_get_str(nvs, MIMI_NVS_KEY_DINGTALK_APP_KEY, tmp_key, &len_key) == ESP_OK && tmp_key[0]) {
            strncpy(s_app_key, tmp_key, sizeof(s_app_key) - 1);
        }
        if (nvs_get_str(nvs, MIMI_NVS_KEY_DINGTALK_APP_SECRET, tmp_secret, &len_secret) == ESP_OK && tmp_secret[0]) {
            strncpy(s_app_secret, tmp_secret, sizeof(s_app_secret) - 1);
        }
        nvs_close(nvs);
    }

    if (s_app_key[0] && s_app_secret[0]) {
        ESP_LOGI(TAG, "DingTalk bot credentials loaded (app_key=%.8s...)", s_app_key);
    } else {
        ESP_LOGW(TAG, "No DingTalk bot credentials. Use CLI: set_dingtalk_creds <APP_KEY> <APP_SECRET>");
    }

    return ESP_OK;
}

esp_err_t dingtalk_bot_start(void)
{
    if (s_app_key[0] == '\0' || s_app_secret[0] == '\0') {
        ESP_LOGW(TAG, "DingTalk bot not configured, skipping webhook start");
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        dingtalk_webhook_task,
        "dingtalk_webhook",
        4096,
        NULL,
        5,
        NULL,
        0);

    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DingTalk webhook mode enabled");
    return ESP_OK;
}

esp_err_t dingtalk_send_message(const char *chat_id, const char *text)
{
    if (s_app_key[0] == '\0' || s_app_secret[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send: no credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get access token first */
    if (dingtalk_get_token() != ESP_OK) {
        return ESP_FAIL;
    }

    /* Build send URL with access token */
    char url[1024];
    snprintf(url, sizeof(url), DINGTALK_SEND_MSG_URL, s_access_token);

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
        cJSON_AddStringToObject(body, "msgtype", "text");
        cJSON *text_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(text_obj, "content", segment);
        cJSON_AddItemToObject(body, "text", text_obj);
        char *json_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        free(segment);

        if (!json_str) {
            offset += chunk;
            all_ok = 0;
            continue;
        }

        ESP_LOGI(TAG, "Sending DingTalk message to chat %s (%d bytes)", chat_id, (int)chunk);
        char *resp = dingtalk_api_call(url, "POST", json_str);
        free(json_str);

        if (resp) {
            process_message(resp);
            free(resp);
        } else {
            ESP_LOGE(TAG, "Failed to send message chunk");
            all_ok = 0;
        }

        offset += chunk;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t dingtalk_set_credentials(const char *app_key, const char *app_secret)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_DINGTALK, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_DINGTALK_APP_KEY, app_key));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_DINGTALK_APP_SECRET, app_secret));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_app_key, app_key, sizeof(s_app_key) - 1);
    strncpy(s_app_secret, app_secret, sizeof(s_app_secret) - 1);

    /* Clear cached token to force re-auth */
    s_access_token[0] = '\0';
    s_token_expire_time = 0;

    ESP_LOGI(TAG, "DingTalk bot credentials saved");
    return ESP_OK;
}
