#include "tool_display_mqtt.h"
#include "mimi_config.h"
#include "hardware/ili9341.h"
#include "hardware/aliyun_mqtt.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_disp_mqtt";

esp_err_t tool_display_mqtt_init(void)
{
#if MIMI_TFT_ENABLED
    esp_err_t ret = ili9341_init();
    if (ret == ESP_OK) {
        ili9341_dashboard_init();
        ESP_LOGI(TAG, "TFT display initialized");
    } else {
        ESP_LOGW(TAG, "TFT display init failed: %s", esp_err_to_name(ret));
    }
#endif

#if MIMI_MQTT_ENABLED
    aliyun_mqtt_init();
    aliyun_mqtt_set_temp_callback(NULL);
    ESP_LOGI(TAG, "MQTT client initialized");
#endif

    return ESP_OK;
}

esp_err_t tool_mqtt_read_temp_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    
    aliyun_temp_data_t data;
    esp_err_t ret = aliyun_mqtt_get_temp_data(&data);
    
    if (ret == ESP_OK && data.temp_valid) {
        snprintf(output, output_size, "{\"temperature\": %.1f, \"unit\": \"celsius\"}", 
                 data.temperature);
    } else {
        snprintf(output, output_size, "{\"error\": \"No temperature data available\"}");
    }
    
    return ret;
}

esp_err_t tool_mqtt_read_humidity_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    
    aliyun_temp_data_t data;
    esp_err_t ret = aliyun_mqtt_get_temp_data(&data);
    
    if (ret == ESP_OK && data.humidity_valid) {
        snprintf(output, output_size, "{\"humidity\": %.1f, \"unit\": \"percent\"}", 
                 data.humidity);
    } else {
        snprintf(output, output_size, "{\"error\": \"No humidity data available\"}");
    }
    
    return ret;
}

esp_err_t tool_mqtt_subscribe_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *topic = cJSON_GetObjectItem(root, "topic");
    if (!topic || !cJSON_IsString(topic)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'topic' field");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = aliyun_mqtt_set_temp_topic(topic->valuestring);
    
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        snprintf(output, output_size, "Subscribed to: %s", topic->valuestring);
    } else {
        snprintf(output, output_size, "Failed to subscribe: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t tool_mqtt_publish_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *topic = cJSON_GetObjectItem(root, "topic");
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    
    if (!topic || !cJSON_IsString(topic) || !payload || !cJSON_IsString(payload)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'topic' or 'payload' field");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = aliyun_mqtt_publish(topic->valuestring, payload->valuestring);
    
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        snprintf(output, output_size, "Published to: %s", topic->valuestring);
    } else {
        snprintf(output, output_size, "Failed to publish: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t tool_tft_show_execute(const char *input_json, char *output, size_t output_size)
{
#if MIMI_TFT_ENABLED
    if (!ili9341_is_initialized()) {
        snprintf(output, output_size, "Error: TFT display not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'action' field");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *action_str = action->valuestring;
    esp_err_t ret = ESP_OK;
    
    if (strcmp(action_str, "dashboard") == 0) {
        ili9341_dashboard_t dash = {0};
        
        cJSON *temp = cJSON_GetObjectItem(root, "temperature");
        cJSON *humid = cJSON_GetObjectItem(root, "humidity");
        cJSON *wifi = cJSON_GetObjectItem(root, "wifi_status");
        cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt_status");
        
        if (temp && cJSON_IsNumber(temp)) {
            dash.temperature = (float)temp->valuedouble;
            dash.temp_valid = true;
        }
        if (humid && cJSON_IsNumber(humid)) {
            dash.humidity = (float)humid->valuedouble;
            dash.humidity_valid = true;
        }
        if (wifi && cJSON_IsString(wifi)) {
            strncpy(dash.wifi_status, wifi->valuestring, sizeof(dash.wifi_status) - 1);
        }
        if (mqtt && cJSON_IsString(mqtt)) {
            strncpy(dash.mqtt_status, mqtt->valuestring, sizeof(dash.mqtt_status) - 1);
        }
        
        ret = ili9341_dashboard_update(&dash);
        snprintf(output, output_size, "Dashboard updated");
        
    } else if (strcmp(action_str, "clear") == 0) {
        ili9341_fill_screen(ILI9341_BLACK);
        snprintf(output, output_size, "Screen cleared");
        
    } else if (strcmp(action_str, "fill") == 0) {
        cJSON *color = cJSON_GetObjectItem(root, "color");
        uint16_t c = ILI9341_BLACK;
        if (color && cJSON_IsString(color)) {
            if (strcmp(color->valuestring, "white") == 0) c = ILI9341_WHITE;
            else if (strcmp(color->valuestring, "red") == 0) c = ILI9341_RED;
            else if (strcmp(color->valuestring, "green") == 0) c = ILI9341_GREEN;
            else if (strcmp(color->valuestring, "blue") == 0) c = ILI9341_BLUE;
            else if (strcmp(color->valuestring, "yellow") == 0) c = ILI9341_YELLOW;
            else if (strcmp(color->valuestring, "cyan") == 0) c = ILI9341_CYAN;
            else if (strcmp(color->valuestring, "orange") == 0) c = ILI9341_ORANGE;
        }
        ili9341_fill_screen(c);
        snprintf(output, output_size, "Screen filled with color");
        
    } else if (strcmp(action_str, "text") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        cJSON *x = cJSON_GetObjectItem(root, "x");
        cJSON *y = cJSON_GetObjectItem(root, "y");
        cJSON *fg = cJSON_GetObjectItem(root, "fg");
        cJSON *bg = cJSON_GetObjectItem(root, "bg");
        
        if (!text || !cJSON_IsString(text)) {
            cJSON_Delete(root);
            snprintf(output, output_size, "Error: missing 'text' field");
            return ESP_ERR_INVALID_ARG;
        }
        
        int px = x ? x->valueint : 10;
        int py = y ? y->valueint : 10;
        uint16_t fgc = fg ? (uint16_t)fg->valueint : ILI9341_WHITE;
        uint16_t bgc = bg ? (uint16_t)bg->valueint : ILI9341_BLACK;
        
        ili9341_draw_string(px, py, text->valuestring, fgc, bgc, FONT_MEDIUM);
        snprintf(output, output_size, "Text displayed");
        
    } else {
        snprintf(output, output_size, "Error: unknown action '%s'", action_str);
        ret = ESP_ERR_INVALID_ARG;
    }
    
    cJSON_Delete(root);
    return ret;
#else
    snprintf(output, output_size, "Error: TFT display not enabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t tool_tft_notification_execute(const char *input_json, char *output, size_t output_size)
{
#if MIMI_TFT_ENABLED
    if (!ili9341_is_initialized()) {
        snprintf(output, output_size, "Error: TFT display not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *message = cJSON_GetObjectItem(root, "message");
    cJSON *duration = cJSON_GetObjectItem(root, "duration_ms");
    
    if (!message || !cJSON_IsString(message)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'message' field");
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t dur = duration ? (uint32_t)duration->valueint : 5000;
    
    esp_err_t ret = ili9341_show_notification(message->valuestring, dur);
    
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        snprintf(output, output_size, "Notification shown: %s", message->valuestring);
    } else {
        snprintf(output, output_size, "Failed to show notification");
    }
    
    return ret;
#else
    snprintf(output, output_size, "Error: TFT display not enabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}