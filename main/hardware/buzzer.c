#include "buzzer.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>

static const char *TAG = "buzzer";

static bool s_initialized = false;
static uint8_t s_volume = 50;  /* 0-100 percent */
static esp_timer_handle_t s_stop_timer = NULL;
static ledc_channel_config_t s_ledc_channel;

/* Note: For passive buzzer, we use LEDC PWM to generate frequency */

static void stop_timer_callback(void *arg)
{
    (void)arg;
    ledc_set_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel, 0);
    ledc_update_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel);
}

esp_err_t buzzer_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Validate GPIO */
    if (MIMI_BUZZER_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Buzzer GPIO not configured, skipping init");
        return ESP_OK;
    }

    /* LEDC timer configuration */
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_2,
        .freq_hz = 440,  /* Start with A4 */
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    /* LEDC channel configuration */
    s_ledc_channel = (ledc_channel_config_t) {
        .gpio_num = MIMI_BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_2,
        .duty = 0,  /* Start silent */
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&s_ledc_channel));

    /* Create one-shot timer for auto-stop */
    esp_timer_create_args_t timer_args = {
        .callback = stop_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "buzzer_stop",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_stop_timer));

    s_initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO %d", MIMI_BUZZER_GPIO);
    return ESP_OK;
}

esp_err_t buzzer_tone(uint16_t freq_hz, uint16_t duration_ms)
{
    if (!s_initialized || MIMI_BUZZER_GPIO == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop any previous timer */
    esp_timer_stop(s_stop_timer);

    if (freq_hz == 0) {
        /* Rest/silence */
        ledc_set_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel, 0);
        ledc_update_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel);
        return ESP_OK;
    }

    /* Set frequency */
    ledc_set_freq(s_ledc_channel.speed_mode, s_ledc_channel.timer_sel, freq_hz);

    /* Set duty cycle based on volume (50% duty for max volume) */
    uint32_t duty = (512 * s_volume) / 100;  /* 10-bit resolution */
    ledc_set_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel, duty);
    ledc_update_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel);

    /* Auto-stop after duration */
    if (duration_ms > 0) {
        esp_timer_start_once(s_stop_timer, duration_ms * 1000);
    }

    return ESP_OK;
}

esp_err_t buzzer_stop(void)
{
    if (!s_initialized || MIMI_BUZZER_GPIO == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_timer_stop(s_stop_timer);
    ledc_set_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel, 0);
    ledc_update_duty(s_ledc_channel.speed_mode, s_ledc_channel.channel);
    return ESP_OK;
}

esp_err_t buzzer_beep(uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    if (!s_initialized || MIMI_BUZZER_GPIO == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < count; i++) {
        buzzer_tone(1000, on_ms);  /* 1kHz beep */
        vTaskDelay(pdMS_TO_TICKS(on_ms + off_ms));
    }
    return ESP_OK;
}

esp_err_t buzzer_melody(const uint16_t *freqs, const uint16_t *durations, uint16_t count)
{
    if (!s_initialized || MIMI_BUZZER_GPIO == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint16_t i = 0; i < count; i++) {
        buzzer_tone(freqs[i], durations[i]);
        vTaskDelay(pdMS_TO_TICKS(durations[i] + 50));  /* Short gap between notes */
    }
    return ESP_OK;
}

void buzzer_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    s_volume = volume;
    ESP_LOGD(TAG, "Volume set to %d%%", volume);
}