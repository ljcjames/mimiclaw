#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * Passive Buzzer Driver
 * 
 * Uses LEDC PWM to generate tones on a passive buzzer.
 */

/**
 * Initialize the buzzer driver.
 * 
 * @return ESP_OK on success
 */
esp_err_t buzzer_init(void);

/**
 * Play a tone at the specified frequency.
 * 
 * @param freq_hz Frequency in Hz (e.g., 440 for A4)
 * @param duration_ms Duration in milliseconds (0 = continuous)
 * @return ESP_OK on success
 */
esp_err_t buzzer_tone(uint16_t freq_hz, uint16_t duration_ms);

/**
 * Stop playing any tone.
 * 
 * @return ESP_OK on success
 */
esp_err_t buzzer_stop(void);

/**
 * Play a beep pattern.
 * 
 * @param count Number of beeps
 * @param on_ms Duration of each beep in ms
 * @param off_ms Pause between beeps in ms
 * @return ESP_OK on success
 */
esp_err_t buzzer_beep(uint8_t count, uint16_t on_ms, uint16_t off_ms);

/**
 * Play a melody from frequency array.
 * 
 * @param freqs Array of frequencies (0 = rest)
 * @param durations Array of note durations in ms
 * @param count Number of notes
 * @return ESP_OK on success
 */
esp_err_t buzzer_melody(const uint16_t *freqs, const uint16_t *durations, uint16_t count);

/**
 * Set volume (0-100 percent).
 * 
 * @param volume Volume percentage (0-100)
 */
void buzzer_set_volume(uint8_t volume);

/* Common note frequencies */
#define NOTE_C3  131
#define NOTE_D3  147
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_G3  196
#define NOTE_A3  220
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988