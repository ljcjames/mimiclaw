#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize all hardware peripherals.
 * Call this once during startup.
 */
esp_err_t tool_hardware_init(void);

/**
 * LED control tool - set color, clear, or adjust brightness.
 * Actions: "set" (r,g,b,index?), "clear", "brightness" (level)
 */
esp_err_t tool_led_execute(const char *input_json, char *output, size_t output_size);

/**
 * Buzzer control tool - play tones, beeps, or stop.
 * Actions: "tone" (freq,duration), "beep" (count,on_ms,off_ms), "stop", "volume" (level)
 */
esp_err_t tool_buzzer_execute(const char *input_json, char *output, size_t output_size);

/**
 * Temperature/humidity sensor tool - read from AHT10 or DHT.
 * Sensors: "aht10", "dht", "all"
 */
esp_err_t tool_sensor_execute(const char *input_json, char *output, size_t output_size);