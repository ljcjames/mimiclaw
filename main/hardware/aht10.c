#include "aht10.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "aht10";

#define AHT10_ADDR          0x38
#define AHT10_CMD_INIT      0xBE
#define AHT10_CMD_TRIGGER   0xAC
#define AHT10_CMD_RESET     0xBA

static i2c_master_dev_handle_t s_dev = NULL;
static aht10_reading_t s_cached = {0};
static TaskHandle_t s_poll_task = NULL;
static uint32_t s_poll_interval_ms = 5000;

static esp_err_t aht10_write_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    uint8_t buf[16];
    buf[0] = cmd;
    if (data && len > 0) {
        memcpy(buf + 1, data, len);
    }
    return i2c_master_transmit(s_dev, buf, 1 + len, 100);
}

static esp_err_t aht10_read_data(uint8_t *data, size_t len)
{
    return i2c_master_receive(s_dev, data, len, 100);
}

static bool aht10_is_busy(void)
{
    uint8_t status;
    if (aht10_read_data(&status, 1) != ESP_OK) {
        return true;
    }
    return (status & 0x80) != 0;
}

esp_err_t aht10_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    /* Validate I2C configuration */
    if (MIMI_I2C_SDA_GPIO == GPIO_NUM_NC || MIMI_I2C_SCL_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "I2C GPIO not configured, skipping AHT10 init");
        return ESP_OK;
    }

    /* Get or create I2C bus */
    i2c_master_bus_handle_t i2c_bus = NULL;
    
    /* Try to get existing bus */
    esp_err_t ret = i2c_master_get_bus_handle(MIMI_I2C_NUM, &i2c_bus);
    if (ret != ESP_OK || i2c_bus == NULL) {
        /* Create new I2C bus */
        i2c_master_bus_config_t bus_config = {
            .i2c_port = MIMI_I2C_NUM,
            .sda_io_num = MIMI_I2C_SDA_GPIO,
            .scl_io_num = MIMI_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ret = i2c_new_master_bus(&bus_config, &i2c_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    /* Add device to bus */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT10_ADDR,
        .scl_speed_hz = 100000,  /* 100kHz */
    };
    ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AHT10 device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Reset and initialize sensor */
    aht10_write_cmd(AHT10_CMD_RESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Send init command: enable calibration */
    uint8_t init_data[] = {0x08, 0x00};
    aht10_write_cmd(AHT10_CMD_INIT, init_data, 2);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "AHT10 initialized");
    return ESP_OK;
}

bool aht10_is_present(void)
{
    if (s_dev == NULL) {
        return false;
    }
    uint8_t status;
    return aht10_read_data(&status, 1) == ESP_OK;
}

esp_err_t aht10_read(aht10_reading_t *reading)
{
    if (s_dev == NULL) {
        reading->valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    /* Trigger measurement */
    uint8_t trigger_cmd[] = {0x33, 0x00};
    esp_err_t ret = aht10_write_cmd(AHT10_CMD_TRIGGER, trigger_cmd, 2);
    if (ret != ESP_OK) {
        reading->valid = false;
        return ret;
    }

    /* Wait for measurement to complete (max 80ms) */
    int timeout = 100;
    while (aht10_is_busy() && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout -= 10;
    }
    if (timeout <= 0) {
        reading->valid = false;
        return ESP_ERR_TIMEOUT;
    }

    /* Read 6 bytes: status + humidity (20bit) + temperature (20bit) */
    uint8_t data[6];
    ret = aht10_read_data(data, 6);
    if (ret != ESP_OK) {
        reading->valid = false;
        return ret;
    }

    /* Parse humidity: ((data[1] << 12) | (data[2] << 4) | (data[3] >> 4)) */
    uint32_t hum_raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    reading->humidity = (hum_raw * 100.0f) / 1048576.0f;  /* 2^20 */

    /* Parse temperature: (((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5]) */
    uint32_t temp_raw = (((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5]);
    reading->temperature = (temp_raw * 200.0f / 1048576.0f) - 50.0f;
    reading->valid = true;

    /* Update cache */
    s_cached = *reading;

    ESP_LOGD(TAG, "T=%.1f°C, H=%.1f%%", reading->temperature, reading->humidity);
    return ESP_OK;
}

esp_err_t aht10_get_cached(aht10_reading_t *reading)
{
    *reading = s_cached;
    return s_cached.valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void poll_task(void *arg)
{
    aht10_reading_t reading;
    while (1) {
        aht10_read(&reading);
        vTaskDelay(pdMS_TO_TICKS(s_poll_interval_ms));
    }
}

esp_err_t aht10_start_polling(uint32_t interval_ms)
{
    if (s_poll_task != NULL) {
        return ESP_OK;
    }

    s_poll_interval_ms = interval_ms < 1000 ? 1000 : interval_ms;

    BaseType_t ret = xTaskCreate(poll_task, "aht10_poll", 3072, NULL, 3, &s_poll_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create polling task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Polling started (interval: %lu ms)", s_poll_interval_ms);
    return ESP_OK;
}

void aht10_stop_polling(void)
{
    if (s_poll_task != NULL) {
        vTaskDelete(s_poll_task);
        s_poll_task = NULL;
        ESP_LOGI(TAG, "Polling stopped");
    }
}