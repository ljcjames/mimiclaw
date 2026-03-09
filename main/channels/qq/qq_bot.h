#pragma once

#include "esp_err.h"

/**
 * Initialize the QQ bot (load credentials from NVS / build-time).
 */
esp_err_t qq_bot_init(void);

/**
 * Start the QQ bot webhook HTTP server for receiving events.
 * Listens on MIMI_QQ_WEBHOOK_PORT.
 */
esp_err_t qq_bot_start(void);

/**
 * Send a text message to a QQ chat.
 * Automatically splits messages longer than MIMI_QQ_MAX_MSG_LEN chars.
 * @param chat_id  QQ user ID or group ID
 * @param text     Message text
 */
esp_err_t qq_send_message(const char *chat_id, const char *text);

/**
 * Save QQ bot credentials to NVS.
 * @param app_id     QQ Bot App ID
 * @param app_secret QQ Bot App Secret
 */
esp_err_t qq_set_credentials(const char *app_id, const char *app_secret);
