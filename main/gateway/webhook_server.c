#include "webhook_server.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "channels/qq/qq_bot.h"
#include "channels/dingtalk/dingtalk_bot.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "webhook";

static httpd_handle_t s_server = NULL;

/* ── QQ Webhook Handler ─────────────────────────────────────── */
static esp_err_t qq_webhook_handler(httpd_req_t *req)
{
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    /* Read request body */
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        return ESP_ERR_NO_MEM;
    }

    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret < 0) {
        free(buf);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "QQ webhook received (%d bytes)", ret);

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON from QQ webhook");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    /* Handle URL verification */
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "url_verification") == 0) {
        cJSON *challenge = cJSON_GetObjectItem(root, "challenge");
        if (challenge && cJSON_IsString(challenge)) {
            ESP_LOGI(TAG, "QQ webhook verification challenge: %s", challenge->valuestring);
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_send(req, challenge->valuestring, strlen(challenge->valuestring));
            cJSON_Delete(root);
            return ESP_OK;
        }
    }

    /* Process message event */
    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (event && cJSON_IsObject(event)) {
        cJSON *message_id = cJSON_GetObjectItem(event, "message_id");
        cJSON *content = cJSON_GetObjectItem(event, "content");
        cJSON *sender = cJSON_GetObjectItem(event, "sender");
        cJSON *sender_id = cJSON_GetObjectItem(sender, "user_id");
        cJSON *group_id = cJSON_GetObjectItem(event, "group_id");

        if (message_id && cJSON_IsString(message_id)) {
            const char *text = content ? content->valuestring : "";
            const char *user_id = sender_id ? sender_id->valuestring : "";
            const char *gid = group_id ? group_id->valuestring : "";

            ESP_LOGI(TAG, "QQ message from user %s in group %s: %.60s%s",
                     user_id, gid, text, strlen(text) > 60 ? "..." : "");

            /* Push to inbound message bus */
            mimi_msg_t msg = {0};
            strncpy(msg.channel, MIMI_CHAN_QQ, sizeof(msg.channel) - 1);
            strncpy(msg.chat_id, gid, sizeof(msg.chat_id) - 1);
            msg.content = strdup(text);

            if (msg.content) {
                if (message_bus_push_inbound(&msg) != ESP_OK) {
                    ESP_LOGW(TAG, "Inbound queue full, dropping QQ message");
                    free(msg.content);
                }
            }
        }
    }

    cJSON_Delete(root);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── DingTalk Webhook Handler ───────────────────────────────── */
static esp_err_t dingtalk_webhook_handler(httpd_req_t *req)
{
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    /* Read request body */
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        return ESP_ERR_NO_MEM;
    }

    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret < 0) {
        free(buf);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "DingTalk webhook received (%d bytes)", ret);

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON from DingTalk webhook");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    /* Handle URL verification */
    cJSON *auth = cJSON_GetObjectItem(root, "auth");
    if (auth && cJSON_IsObject(auth)) {
        cJSON *signature = cJSON_GetObjectItem(auth, "signature");
        cJSON *timestamp = cJSON_GetObjectItem(auth, "timestamp");
        cJSON *nonce = cJSON_GetObjectItem(auth, "nonce");

        if (signature && timestamp && nonce) {
            ESP_LOGI(TAG, "DingTalk webhook verification: sig=%s, ts=%s, nonce=%s",
                     signature->valuestring, timestamp->valuestring, nonce->valuestring);
            httpd_resp_set_type(req, "application/json");
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "errcode", "0");
            cJSON_AddStringToObject(resp, "errmsg", "ok");
            char *json_str = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
            httpd_resp_send(req, json_str, strlen(json_str));
            free(json_str);
            return ESP_OK;
        }
    }

    /* Process message event */
    cJSON *errcode = cJSON_GetObjectItem(root, "errcode");
    if (errcode && cJSON_IsNumber(errcode) && errcode->valueint == 0) {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (result && cJSON_IsObject(result)) {
            cJSON *message_id = cJSON_GetObjectItem(result, "messageId");
            if (message_id && cJSON_IsString(message_id)) {
                ESP_LOGI(TAG, "DingTalk message received (message_id=%s)", message_id->valuestring);
                /* TODO: Parse message content and push to inbound bus */
            }
        }
    }

    cJSON_Delete(root);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── Webhook Server Start/Stop ─────────────────────────────── */
esp_err_t webhook_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = MIMI_WEBHOOK_PORT;
    config.ctrl_port = MIMI_WEBHOOK_PORT + 1;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webhook server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register QQ webhook */
    httpd_uri_t qq_uri = {
        .uri = MIMI_QQ_WEBHOOK_PATH,
        .method = HTTP_POST,
        .handler = qq_webhook_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &qq_uri);

    ESP_LOGI(TAG, "QQ webhook server started on port %d%s", MIMI_QQ_WEBHOOK_PORT, MIMI_QQ_WEBHOOK_PATH);

    /* Register DingTalk webhook */
    httpd_uri_t ding_uri = {
        .uri = MIMI_DINGTALK_WEBHOOK_PATH,
        .method = HTTP_POST,
        .handler = dingtalk_webhook_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &ding_uri);

    ESP_LOGI(TAG, "DingTalk webhook server started on port %d%s", MIMI_DINGTALK_WEBHOOK_PORT, MIMI_DINGTALK_WEBHOOK_PATH);

    return ESP_OK;
}

esp_err_t webhook_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Webhook server stopped");
    }
    return ESP_OK;
}