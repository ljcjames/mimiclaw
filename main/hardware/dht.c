#include "dht.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "dht";

#define DHT_START_SIGNAL_MS    20
#define DHT_RESPONSE_WAIT_US   80
#define DHT_BIT_THRESHOLD_US   50

static dht_type_t s_type = DHT_TYPE_DHT11;
static gpio_num_t s_gpio = GPIO_NUM_NC;
static dht_reading_t s_cached = {0};
static TaskHandle_t s_poll_task = NULL;
static uint32_t s_poll_interval_ms = 2000;

/* Microsecond delay using esp_timer */
static inline void delay_us(uint32_t us)
{
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < us) {
        /* busy wait */
    }
}

/* Wait for pin to reach specified level with timeout */
static esp_err_t wait_for_level(int level, uint32_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_gpio) != level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

esp_err_t dht_init(dht_type_t type, gpio_num_t gpio)
{
    s_type = type;
    s_gpio = gpio;

    if (gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "DHT GPIO not configured");
        return ESP_OK;
    }

    /* Configure GPIO as input with pull-up */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", gpio, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "DHT%d initialized on GPIO %d", type == DHT_TYPE_DHT11 ? 11 : 22, gpio);
    return ESP_OK;
}

static esp_err_t dht_read_raw(uint8_t data[5])
{
    if (s_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Send start signal: pull low for at least 18ms (DHT11) or 1-10ms (DHT22) */
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT_START_SIGNAL_MS));

    /* Pull high for 20-40us */
    gpio_set_level(s_gpio, 1);
    delay_us(30);

    /* Switch to input mode */
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);

    /* Wait for DHT response: low for 80us */
    if (wait_for_level(0, 100) != ESP_OK) {
        ESP_LOGW(TAG, "No response from DHT (low)");
        return ESP_ERR_TIMEOUT;
    }

    /* Wait for high: 80us */
    if (wait_for_level(1, 100) != ESP_OK) {
        ESP_LOGW(TAG, "No response from DHT (high)");
        return ESP_ERR_TIMEOUT;
    }

    /* Wait for low: start of data transmission */
    if (wait_for_level(0, 100) != ESP_OK) {
        ESP_LOGW(TAG, "No data start from DHT");
        return ESP_ERR_TIMEOUT;
    }

    /* Read 40 bits (5 bytes) */
    for (int i = 0; i < 5; i++) {
        data[i] = 0;
        for (int j = 7; j >= 0; j--) {
            /* Wait for high (start of bit) */
            if (wait_for_level(1, 100) != ESP_OK) {
                ESP_LOGW(TAG, "Timeout waiting for bit %d.%d high", i, j);
                return ESP_ERR_TIMEOUT;
            }

            /* Measure high pulse duration */
            int64_t start = esp_timer_get_time();
            if (wait_for_level(0, 100) != ESP_OK) {
                /* Last bit might not have trailing low */
                if (i == 4 && j == 0) {
                    /* Measure remaining time */
                    int duration = (int)(esp_timer_get_time() - start);
                    data[i] |= (duration > 40) ? (1 << j) : 0;
                    break;
                }
                ESP_LOGW(TAG, "Timeout waiting for bit %d.%d low", i, j);
                return ESP_ERR_TIMEOUT;
            }
            int duration = (int)(esp_timer_get_time() - start);

            /* ~26-28us = 0, ~70us = 1 */
            if (duration > DHT_BIT_THRESHOLD_US) {
                data[i] |= (1 << j);
            }
        }
    }

    /* Verify checksum */
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "Checksum mismatch: calc=0x%02X, recv=0x%02X", checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t dht_read(dht_reading_t *reading)
{
    if (s_gpio == GPIO_NUM_NC) {
        reading->valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[5];
    esp_err_t ret = dht_read_raw(data);
    if (ret != ESP_OK) {
        reading->valid = false;
        return ret;
    }

    if (s_type == DHT_TYPE_DHT11) {
        /* DHT11: integer values */
        reading->humidity = data[0] + data[1] * 0.1f;
        reading->temperature = data[2] + data[3] * 0.1f;
    } else {
        /* DHT22: 16-bit signed values, 0.1 resolution */
        int16_t hum_raw = (data[0] << 8) | data[1];
        int16_t temp_raw = (data[2] << 8) | data[3];
        
        reading->humidity = hum_raw * 0.1f;
        
        /* Handle negative temperature (bit 15 = sign) */
        if (temp_raw & 0x8000) {
            temp_raw = -(temp_raw & 0x7FFF);
        }
        reading->temperature = temp_raw * 0.1f;
    }

    reading->valid = true;
    s_cached = *reading;

    ESP_LOGD(TAG, "T=%.1f°C, H=%.1f%%", reading->temperature, reading->humidity);
    return ESP_OK;
}

esp_err_t dht_get_cached(dht_reading_t *reading)
{
    *reading = s_cached;
    return s_cached.valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void poll_task(void *arg)
{
    dht_reading_t reading;
    while (1) {
        /* DHT sensors need at least 2 seconds between reads */
        vTaskDelay(pdMS_TO_TICKS(s_poll_interval_ms));
        dht_read(&reading);
    }
}

esp_err_t dht_start_polling(uint32_t interval_ms)
{
    if (s_poll_task != NULL) {
        return ESP_OK;
    }

    /* Minimum 2 seconds for DHT sensors */
    s_poll_interval_ms = interval_ms < 2000 ? 2000 : interval_ms;

    BaseType_t ret = xTaskCreate(poll_task, "dht_poll", 3072, NULL, 3, &s_poll_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create polling task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Polling started (interval: %lu ms)", s_poll_interval_ms);
    return ESP_OK;
}

void dht_stop_polling(void)
{
    if (s_poll_task != NULL) {
        vTaskDelete(s_poll_task);
        s_poll_task = NULL;
        ESP_LOGI(TAG, "Polling stopped");
    }
}