#include "aliyun_mqtt.h"
#include "mimi_config.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "aliyun_mqtt";

/* MQTT client handle */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static SemaphoreHandle_t s_mqtt_mutex = NULL;
static bool s_connected = false;

/* Credentials */
static char s_product_key[64] = MIMI_SECRET_ALIYUN_PRODUCT_KEY;
static char s_device_name[64] = MIMI_SECRET_ALIYUN_DEVICE_NAME;
static char s_device_secret[128] = MIMI_SECRET_ALIYUN_DEVICE_SECRET;

/* Subscription topic for temp/humidity */
static char s_temp_topic[ALIYUN_MQTT_MAX_TOPIC] = "";

/* Latest temperature/humidity data */
static aliyun_temp_data_t s_temp_data = {0};
static aliyun_temp_callback_t s_temp_callback = NULL;

/* NVS namespace */
#define ALIYUN_NVS_NAMESPACE "aliyun_mqtt"
#define ALIYUN_NVS_KEY_PK    "product_key"
#define ALIYUN_NVS_KEY_DN    "device_name"
#define ALIYUN_NVS_KEY_DS    "device_secret"
#define ALIYUN_NVS_KEY_TOPIC "temp_topic"

static void bin_to_hex(const unsigned char *bin, size_t bin_len, char *hex, size_t hex_size)
{
    if (hex_size < bin_len * 2 + 1) return;
    for (size_t i = 0; i < bin_len; i++) {
        sprintf(hex + i * 2, "%02x", bin[i]);
    }
    hex[bin_len * 2] = '\0';
}

static int generate_sign(char *client_id, size_t client_id_size,
                         char *username, size_t username_size,
                         char *password, size_t password_size)
{
    if (!s_product_key[0] || !s_device_name[0] || !s_device_secret[0]) {
        ESP_LOGW(TAG, "Aliyun credentials not configured");
        return -1;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t timestamp_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    
    char ts_str[24];
    snprintf(ts_str, sizeof(ts_str), "%lld", (long long)timestamp_ms);
    
    int ret = snprintf(client_id, client_id_size, 
                       "%s.%s|securemode=2,signmethod=hmacsha256,timestamp=%s|",
                       s_product_key, s_device_name, ts_str);
    if (ret < 0 || (size_t)ret >= client_id_size) return -2;
    
    ret = snprintf(username, username_size, "%s&%s", s_device_name, s_product_key);
    if (ret < 0 || (size_t)ret >= username_size) return -3;
    
    char sign_content[512];
    ret = snprintf(sign_content, sizeof(sign_content),
                   "clientId%s.%sdeviceName%sproductKey%stimestamp%s",
                   s_product_key, s_device_name, s_device_name, s_product_key, ts_str);
    if (ret < 0 || (size_t)ret >= sizeof(sign_content)) return -4;
    
    unsigned char hmac_result[32];
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) return -5;
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    
    int err = mbedtls_md_setup(&ctx, md_info, 1);
    if (err != 0) { mbedtls_md_free(&ctx); return -6; }
    
    err = mbedtls_md_hmac_starts(&ctx, (const unsigned char *)s_device_secret, strlen(s_device_secret));
    if (err != 0) { mbedtls_md_free(&ctx); return -7; }
    
    err = mbedtls_md_hmac_update(&ctx, (const unsigned char *)sign_content, strlen(sign_content));
    if (err != 0) { mbedtls_md_free(&ctx); return -8; }
    
    err = mbedtls_md_hmac_finish(&ctx, hmac_result);
    if (err != 0) { mbedtls_md_free(&ctx); return -9; }
    
    mbedtls_md_free(&ctx);
    bin_to_hex(hmac_result, sizeof(hmac_result), password, password_size);
    
    return 0;
}

static void parse_temp_humidity(const char *payload, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) return;
    
    cJSON *temp = cJSON_GetObjectItem(root, "temperature");
    cJSON *humid = cJSON_GetObjectItem(root, "humidity");
    
    if (s_mqtt_mutex) xSemaphoreTake(s_mqtt_mutex, portMAX_DELAY);
    
    bool updated = false;
    
    if (temp && cJSON_IsNumber(temp)) {
        s_temp_data.temperature = (float)temp->valuedouble;
        s_temp_data.temp_valid = true;
        updated = true;
        ESP_LOGI(TAG, "Temperature: %.1f C", s_temp_data.temperature);
    }
    
    if (humid && cJSON_IsNumber(humid)) {
        s_temp_data.humidity = (float)humid->valuedouble;
        s_temp_data.humidity_valid = true;
        updated = true;
        ESP_LOGI(TAG, "Humidity: %.1f %%", s_temp_data.humidity);
    }
    
    if (updated) {
        s_temp_data.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_temp_callback) {
            s_temp_callback(&s_temp_data);
        }
    }
    
    if (s_mqtt_mutex) xSemaphoreGive(s_mqtt_mutex);
    
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, 
                                int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_connected = true;
            if (s_temp_topic[0]) {
                esp_mqtt_client_subscribe(s_mqtt_client, s_temp_topic, 0);
                ESP_LOGI(TAG, "Subscribed to: %s", s_temp_topic);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_connected = false;
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT message: topic=%.*s", event->topic_len, event->topic);
            parse_temp_humidity(event->data, event->data_len);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
            
        default:
            break;
    }
}

esp_err_t aliyun_mqtt_init(void)
{
    s_mqtt_mutex = xSemaphoreCreateMutex();
    if (!s_mqtt_mutex) return ESP_ERR_NO_MEM;
    
    nvs_handle_t nvs;
    if (nvs_open(ALIYUN_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[128];
        size_t len;
        
        len = sizeof(tmp);
        if (nvs_get_str(nvs, ALIYUN_NVS_KEY_PK, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_product_key, tmp, sizeof(s_product_key) - 1);
        }
        
        len = sizeof(tmp);
        if (nvs_get_str(nvs, ALIYUN_NVS_KEY_DN, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_device_name, tmp, sizeof(s_device_name) - 1);
        }
        
        len = sizeof(tmp);
        if (nvs_get_str(nvs, ALIYUN_NVS_KEY_DS, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_device_secret, tmp, sizeof(s_device_secret) - 1);
        }
        
        len = sizeof(s_temp_topic);
        nvs_get_str(nvs, ALIYUN_NVS_KEY_TOPIC, s_temp_topic, &len);
        
        nvs_close(nvs);
    }
    
    if (s_product_key[0] && s_device_name[0] && s_device_secret[0]) {
        ESP_LOGI(TAG, "Aliyun IoT credentials loaded (pk=%.8s...)", s_product_key);
    } else {
        ESP_LOGW(TAG, "Aliyun IoT credentials not configured");
    }
    
    return ESP_OK;
}

esp_err_t aliyun_mqtt_start(void)
{
    if (s_mqtt_client) {
        ESP_LOGW(TAG, "MQTT client already running");
        return ESP_OK;
    }
    
    if (!s_product_key[0] || !s_device_name[0] || !s_device_secret[0]) {
        ESP_LOGE(TAG, "Aliyun credentials not set");
        return ESP_ERR_INVALID_STATE;
    }
    
    char client_id[ALIYUN_MQTT_MAX_CLIENT_ID];
    char username[ALIYUN_MQTT_MAX_USERNAME];
    char password[ALIYUN_MQTT_MAX_PASSWORD];
    
    if (generate_sign(client_id, sizeof(client_id), 
                      username, sizeof(username), 
                      password, sizeof(password)) != 0) {
        ESP_LOGE(TAG, "Failed to generate MQTT sign");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "ClientID: %s", client_id);
    ESP_LOGI(TAG, "Username: %s", username);
    
    char broker_uri[128];
    snprintf(broker_uri, sizeof(broker_uri), 
             "mqtt://%s.iot-as-mqtt.cn-shanghai.aliyuncs.com:1883", 
             s_product_key);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.client_id = client_id,
        .credentials.username = username,
        .credentials.authentication.password = password,
    };
    
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, 
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
    
    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

void aliyun_mqtt_stop(void)
{
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_connected = false;
    }
}

bool aliyun_mqtt_is_connected(void)
{
    return s_connected;
}

void aliyun_mqtt_set_temp_callback(aliyun_temp_callback_t callback)
{
    s_temp_callback = callback;
}

esp_err_t aliyun_mqtt_get_temp_data(aliyun_temp_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    
    if (s_mqtt_mutex) xSemaphoreTake(s_mqtt_mutex, portMAX_DELAY);
    *data = s_temp_data;
    bool valid = s_temp_data.temp_valid || s_temp_data.humidity_valid;
    if (s_mqtt_mutex) xSemaphoreGive(s_mqtt_mutex);
    
    return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t aliyun_mqtt_publish(const char *topic, const char *payload)
{
    if (!s_mqtt_client || !s_connected) return ESP_ERR_INVALID_STATE;
    if (!topic || !payload) return ESP_ERR_INVALID_ARG;
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t aliyun_mqtt_subscribe(const char *topic)
{
    if (!s_mqtt_client || !s_connected) return ESP_ERR_INVALID_STATE;
    if (!topic) return ESP_ERR_INVALID_ARG;
    
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t aliyun_mqtt_set_credentials(const char *product_key, 
                                       const char *device_name,
                                       const char *device_secret)
{
    if (!product_key || !device_name || !device_secret) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(ALIYUN_NVS_NAMESPACE, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, ALIYUN_NVS_KEY_PK, product_key));
    ESP_ERROR_CHECK(nvs_set_str(nvs, ALIYUN_NVS_KEY_DN, device_name));
    ESP_ERROR_CHECK(nvs_set_str(nvs, ALIYUN_NVS_KEY_DS, device_secret));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    
    strncpy(s_product_key, product_key, sizeof(s_product_key) - 1);
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    strncpy(s_device_secret, device_secret, sizeof(s_device_secret) - 1);
    
    ESP_LOGI(TAG, "Aliyun credentials saved");
    return ESP_OK;
}

esp_err_t aliyun_mqtt_set_temp_topic(const char *topic)
{
    if (!topic) return ESP_ERR_INVALID_ARG;
    
    strncpy(s_temp_topic, topic, sizeof(s_temp_topic) - 1);
    
    nvs_handle_t nvs;
    if (nvs_open(ALIYUN_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, ALIYUN_NVS_KEY_TOPIC, topic);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    
    if (s_mqtt_client && s_connected && s_temp_topic[0]) {
        esp_mqtt_client_subscribe(s_mqtt_client, s_temp_topic, 0);
        ESP_LOGI(TAG, "Subscribed to: %s", s_temp_topic);
    }
    
    return ESP_OK;
}