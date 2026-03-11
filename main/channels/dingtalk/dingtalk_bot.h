#pragma once

#include "esp_err.h"

/**
 * Initialize the DingTalk bot (load credentials from NVS / build-time).
 */
esp_err_t dingtalk_bot_init(void);

/**
 * Start the DingTalk webhook HTTP server for receiving events.
 * Listens on MIMI_DINGTALK_WEBHOOK_PORT.
 */
esp_err_t dingtalk_bot_start(void);

/**
 * Send a text message to a DingTalk chat.
 * Automatically splits messages longer than MIMI_DINGTALK_MAX_MSG_LEN chars.
 * @param chat_id  DingTalk chat ID (open_conversation_id)
 * @param text     Message text
 */
esp_err_t dingtalk_send_message(const char *chat_id, const char *text);

/**
 * Save DingTalk bot credentials to NVS.
 * @param app_key     DingTalk App Key
 * @param app_secret  DingTalk App Secret
 */
esp_err_t dingtalk_set_credentials(const char *app_key, const char *app_secret);
