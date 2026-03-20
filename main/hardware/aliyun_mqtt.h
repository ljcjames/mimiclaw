#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Maximum sizes */
#define ALIYUN_MQTT_MAX_CLIENT_ID   128
#define ALIYUN_MQTT_MAX_USERNAME    128
#define ALIYUN_MQTT_MAX_PASSWORD    128
#define ALIYUN_MQTT_MAX_TOPIC       128
#define ALIYUN_MQTT_MAX_PAYLOAD     512

/* Temperature/Humidity data from MQTT */
typedef struct {
    float temperature;
    float humidity;
    bool temp_valid;
    bool humidity_valid;
    uint32_t last_update_ms;
} aliyun_temp_data_t;

/* Callback for temperature/humidity updates */
typedef void (*aliyun_temp_callback_t)(const aliyun_temp_data_t *data);

/**
 * Initialize Aliyun MQTT client.
 * Credentials are read from mimi_config.h or NVS.
 */
esp_err_t aliyun_mqtt_init(void);

/**
 * Start MQTT connection.
 * Blocks until connected or timeout.
 */
esp_err_t aliyun_mqtt_start(void);

/**
 * Stop MQTT connection.
 */
void aliyun_mqtt_stop(void);

/**
 * Check if MQTT is connected.
 */
bool aliyun_mqtt_is_connected(void);

/**
 * Set callback for temperature/humidity updates.
 */
void aliyun_mqtt_set_temp_callback(aliyun_temp_callback_t callback);

/**
 * Get latest temperature/humidity data.
 */
esp_err_t aliyun_mqtt_get_temp_data(aliyun_temp_data_t *data);

/**
 * Publish message to a topic.
 */
esp_err_t aliyun_mqtt_publish(const char *topic, const char *payload);

/**
 * Subscribe to a topic.
 */
esp_err_t aliyun_mqtt_subscribe(const char *topic);

/**
 * Set credentials (saves to NVS).
 */
esp_err_t aliyun_mqtt_set_credentials(const char *product_key, 
                                       const char *device_name,
                                       const char *device_secret);

/**
 * Set subscription topic for temperature/humidity.
 * Topic format: /${ProductKey}/${DeviceName}/user/xxx
 */
esp_err_t aliyun_mqtt_set_temp_topic(const char *topic);