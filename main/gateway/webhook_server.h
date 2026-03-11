#pragma once

#include "esp_err.h"

/**
 * Start the webhook HTTP server for receiving events from QQ and DingTalk.
 * Listens on MIMI_WEBHOOK_PORT (default 18790).
 */
esp_err_t webhook_server_start(void);

/**
 * Stop the webhook HTTP server.
 */
esp_err_t webhook_server_stop(void);
