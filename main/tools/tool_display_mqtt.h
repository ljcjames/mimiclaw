#pragma once

#include "esp_err.h"

/**
 * Initialize display and MQTT tools.
 */
esp_err_t tool_display_mqtt_init(void);

/**
 * Execute mqtt_read_temp tool.
 */
esp_err_t tool_mqtt_read_temp_execute(const char *input_json, char *output, size_t output_size);

/**
 * Execute mqtt_read_humidity tool.
 */
esp_err_t tool_mqtt_read_humidity_execute(const char *input_json, char *output, size_t output_size);

/**
 * Execute mqtt_subscribe tool.
 */
esp_err_t tool_mqtt_subscribe_execute(const char *input_json, char *output, size_t output_size);

/**
 * Execute mqtt_publish tool.
 */
esp_err_t tool_mqtt_publish_execute(const char *input_json, char *output, size_t output_size);

/**
 * Execute tft_show tool.
 */
esp_err_t tool_tft_show_execute(const char *input_json, char *output, size_t output_size);

/**
 * Execute tft_notification tool.
 */
esp_err_t tool_tft_notification_execute(const char *input_json, char *output, size_t output_size);