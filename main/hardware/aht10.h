#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * AHT10/AHT20 Temperature and Humidity Sensor Driver
 * 
 * I2C interface, high precision sensor.
 */

/**
 * Sensor reading result.
 */
typedef struct {
    float temperature;      /* Temperature in Celsius */
    float humidity;         /* Relative humidity in percent */
    bool valid;             /* True if reading is valid */
} aht10_reading_t;

/**
 * Initialize the AHT10/AHT20 sensor.
 * 
 * @return ESP_OK on success
 */
esp_err_t aht10_init(void);

/**
 * Check if sensor is present and responding.
 * 
 * @return true if sensor is detected
 */
bool aht10_is_present(void);

/**
 * Read temperature and humidity from sensor.
 * This triggers a measurement and waits for result.
 * 
 * @param reading Pointer to store the reading
 * @return ESP_OK on success
 */
esp_err_t aht10_read(aht10_reading_t *reading);

/**
 * Get last cached reading (non-blocking).
 * 
 * @param reading Pointer to store the reading
 * @return ESP_OK if cached data is available
 */
esp_err_t aht10_get_cached(aht10_reading_t *reading);

/**
 * Start background polling task.
 * 
 * @param interval_ms Polling interval in milliseconds
 * @return ESP_OK on success
 */
esp_err_t aht10_start_polling(uint32_t interval_ms);

/**
 * Stop background polling task.
 */
void aht10_stop_polling(void);