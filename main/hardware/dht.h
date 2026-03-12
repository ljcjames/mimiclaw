#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * DHT11/DHT22 Temperature and Humidity Sensor Driver
 * 
 * Single-wire protocol, low cost sensors.
 */

/**
 * Sensor type.
 */
typedef enum {
    DHT_TYPE_DHT11 = 0,     /* DHT11: integer precision, 0-50°C, 20-90% RH */
    DHT_TYPE_DHT22 = 1,     /* DHT22/AM2302: 0.1°C precision, -40~80°C, 0-100% RH */
} dht_type_t;

/**
 * Sensor reading result.
 */
typedef struct {
    float temperature;      /* Temperature in Celsius */
    float humidity;         /* Relative humidity in percent */
    bool valid;             /* True if reading is valid */
} dht_reading_t;

/**
 * Initialize the DHT sensor.
 * 
 * @param type DHT11 or DHT22
 * @param gpio GPIO pin connected to DATA line
 * @return ESP_OK on success
 */
esp_err_t dht_init(dht_type_t type, gpio_num_t gpio);

/**
 * Read temperature and humidity from sensor.
 * This blocks for ~5ms during communication.
 * 
 * @param reading Pointer to store the reading
 * @return ESP_OK on success
 */
esp_err_t dht_read(dht_reading_t *reading);

/**
 * Get last cached reading (non-blocking).
 * 
 * @param reading Pointer to store the reading
 * @return ESP_OK if cached data is available
 */
esp_err_t dht_get_cached(dht_reading_t *reading);

/**
 * Start background polling task.
 * Note: DHT sensors should not be read faster than once per 2 seconds.
 * 
 * @param interval_ms Polling interval in milliseconds (min 2000)
 * @return ESP_OK on success
 */
esp_err_t dht_start_polling(uint32_t interval_ms);

/**
 * Stop background polling task.
 */
void dht_stop_polling(void);