#include "tool_hardware.h"
#include "mimi_config.h"
#include "hardware/buzzer.h"
#include "hardware/aht10.h"
#include "hardware/dht.h"

/* Direct use of espressif__led_strip component */
#include "led_strip.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"
#include "driver/gpio.h"

static const char *TAG = "tool_hw";

/* LED strip state */
static led_strip_handle_t s_led_strip = NULL;
static uint8_t s_led_brightness = 100;

/* ============================================================================
 * LED Control - Direct use of espressif__led_strip
 * ============================================================================ */

static esp_err_t led_init(void)
{
    if (s_led_strip != NULL) {
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = MIMI_LED_STRIP_GPIO,
        .max_leds = MIMI_LED_STRIP_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret == ESP_OK) {
        led_strip_clear(s_led_strip);
        ESP_LOGI(TAG, "LED strip initialized: %d LEDs on GPIO %d", 
                 MIMI_LED_STRIP_COUNT, MIMI_LED_STRIP_GPIO);
    } else {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static uint8_t scale_brightness(uint8_t value)
{
    return (uint8_t)((value * s_led_brightness) / 100);
}

esp_err_t tool_led_execute(const char *input_json, char *output, size_t output_size)
{
    /* Ensure LED strip is initialized */
    if (s_led_strip == NULL) {
        if (led_init() != ESP_OK || s_led_strip == NULL) {
            snprintf(output, output_size, "LED strip not available");
            return ESP_ERR_INVALID_STATE;
        }
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

    esp_err_t ret = ESP_OK;
    const char *action_str = action->valuestring;

    if (strcmp(action_str, "set") == 0) {
        cJSON *r = cJSON_GetObjectItem(root, "r");
        cJSON *g = cJSON_GetObjectItem(root, "g");
        cJSON *b = cJSON_GetObjectItem(root, "b");
        cJSON *index = cJSON_GetObjectItem(root, "index");

        if (!r || !g || !b) {
            cJSON_Delete(root);
            snprintf(output, output_size, "Error: missing r/g/b values");
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t rv = scale_brightness((uint8_t)cJSON_GetNumberValue(r));
        uint8_t gv = scale_brightness((uint8_t)cJSON_GetNumberValue(g));
        uint8_t bv = scale_brightness((uint8_t)cJSON_GetNumberValue(b));

        if (index && cJSON_IsNumber(index)) {
            uint16_t idx = (uint16_t)cJSON_GetNumberValue(index);
            ret = led_strip_set_pixel(s_led_strip, idx, rv, gv, bv);
            led_strip_refresh(s_led_strip);
            snprintf(output, output_size, "LED %d set to RGB(%d,%d,%d)", idx, rv, gv, bv);
        } else {
            for (uint16_t i = 0; i < MIMI_LED_STRIP_COUNT; i++) {
                led_strip_set_pixel(s_led_strip, i, rv, gv, bv);
            }
            led_strip_refresh(s_led_strip);
            snprintf(output, output_size, "All LEDs set to RGB(%d,%d,%d)", rv, gv, bv);
        }
    } else if (strcmp(action_str, "clear") == 0) {
        ret = led_strip_clear(s_led_strip);
        snprintf(output, output_size, "LEDs cleared");
    } else if (strcmp(action_str, "brightness") == 0) {
        cJSON *level = cJSON_GetObjectItem(root, "level");
        if (!level) {
            cJSON_Delete(root);
            snprintf(output, output_size, "Error: missing brightness level");
            return ESP_ERR_INVALID_ARG;
        }
        s_led_brightness = (uint8_t)cJSON_GetNumberValue(level);
        if (s_led_brightness > 100) s_led_brightness = 100;
        snprintf(output, output_size, "Brightness set to %d%%", s_led_brightness);
    } else {
        snprintf(output, output_size, "Error: unknown action '%s'", action_str);
        ret = ESP_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return ret;
}

/* ============================================================================
 * Buzzer Control Tool
 * ============================================================================ */

esp_err_t tool_buzzer_execute(const char *input_json, char *output, size_t output_size)
{
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

    esp_err_t ret = ESP_OK;
    const char *action_str = action->valuestring;

    if (strcmp(action_str, "tone") == 0) {
        cJSON *freq = cJSON_GetObjectItem(root, "freq");
        cJSON *duration = cJSON_GetObjectItem(root, "duration");

        uint16_t freq_hz = freq ? (uint16_t)cJSON_GetNumberValue(freq) : 1000;
        uint16_t dur_ms = duration ? (uint16_t)cJSON_GetNumberValue(duration) : 500;

        ret = buzzer_tone(freq_hz, dur_ms);
        snprintf(output, output_size, "Playing %d Hz for %d ms", freq_hz, dur_ms);
    } else if (strcmp(action_str, "beep") == 0) {
        cJSON *count = cJSON_GetObjectItem(root, "count");
        cJSON *on_ms = cJSON_GetObjectItem(root, "on_ms");
        cJSON *off_ms = cJSON_GetObjectItem(root, "off_ms");

        uint8_t cnt = count ? (uint8_t)cJSON_GetNumberValue(count) : 1;
        uint16_t on = on_ms ? (uint16_t)cJSON_GetNumberValue(on_ms) : 100;
        uint16_t off = off_ms ? (uint16_t)cJSON_GetNumberValue(off_ms) : 100;

        ret = buzzer_beep(cnt, on, off);
        snprintf(output, output_size, "Beeped %d times", cnt);
    } else if (strcmp(action_str, "stop") == 0) {
        ret = buzzer_stop();
        snprintf(output, output_size, "Buzzer stopped");
    } else if (strcmp(action_str, "volume") == 0) {
        cJSON *level = cJSON_GetObjectItem(root, "level");
        if (!level) {
            cJSON_Delete(root);
            snprintf(output, output_size, "Error: missing volume level");
            return ESP_ERR_INVALID_ARG;
        }
        uint8_t volume = (uint8_t)cJSON_GetNumberValue(level);
        buzzer_set_volume(volume);
        snprintf(output, output_size, "Volume set to %d%%", volume);
    } else {
        snprintf(output, output_size, "Error: unknown action '%s'", action_str);
        ret = ESP_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return ret;
}

/* ============================================================================
 * Temperature/Humidity Sensor Tool
 * ============================================================================ */

esp_err_t tool_sensor_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *sensor = cJSON_GetObjectItem(root, "sensor");
    if (!sensor || !cJSON_IsString(sensor)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'sensor' field (aht10 or dht)");
        return ESP_ERR_INVALID_ARG;
    }

    const char *sensor_type = sensor->valuestring;
    esp_err_t ret = ESP_OK;

    if (strcmp(sensor_type, "aht10") == 0) {
        aht10_reading_t reading;
        ret = aht10_read(&reading);
        if (ret == ESP_OK && reading.valid) {
            snprintf(output, output_size, 
                     "AHT10: Temperature=%.1f°C, Humidity=%.1f%%", 
                     reading.temperature, reading.humidity);
        } else {
            snprintf(output, output_size, "AHT10 read failed: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(sensor_type, "dht") == 0) {
        dht_reading_t reading;
        ret = dht_read(&reading);
        if (ret == ESP_OK && reading.valid) {
            snprintf(output, output_size, 
                     "DHT: Temperature=%.1f°C, Humidity=%.1f%%", 
                     reading.temperature, reading.humidity);
        } else {
            snprintf(output, output_size, "DHT read failed: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(sensor_type, "all") == 0) {
        int offset = 0;
        output[0] = '\0';

        aht10_reading_t aht_reading;
        if (aht10_read(&aht_reading) == ESP_OK && aht_reading.valid) {
            offset += snprintf(output + offset, output_size - offset,
                              "AHT10: %.1f°C, %.1f%% | ", 
                              aht_reading.temperature, aht_reading.humidity);
        }

        dht_reading_t dht_reading;
        if (dht_read(&dht_reading) == ESP_OK && dht_reading.valid) {
            offset += snprintf(output + offset, output_size - offset,
                              "DHT: %.1f°C, %.1f%%", 
                              dht_reading.temperature, dht_reading.humidity);
        }

        if (offset == 0) {
            snprintf(output, output_size, "No sensors available");
        }
    } else {
        snprintf(output, output_size, "Error: unknown sensor '%s'", sensor_type);
        ret = ESP_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return ret;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t tool_hardware_init(void)
{
    /* LED strip is initialized lazily in tool_led_execute */
    
    /* Initialize buzzer */
#if MIMI_BUZZER_GPIO != GPIO_NUM_NC
    buzzer_init();
#endif

    /* Initialize AHT10 sensor */
#if MIMI_I2C_SDA_GPIO != GPIO_NUM_NC && MIMI_I2C_SCL_GPIO != GPIO_NUM_NC
    aht10_init();
#endif

    /* Initialize DHT sensor */
#if MIMI_DHT_GPIO != GPIO_NUM_NC
    dht_init(MIMI_DHT_TYPE, MIMI_DHT_GPIO);
#endif

    ESP_LOGI(TAG, "Hardware peripherals initialized");
    return ESP_OK;
}