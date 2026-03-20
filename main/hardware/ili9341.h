#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Display dimensions */
#define ILI9341_WIDTH   240
#define ILI9341_HEIGHT  320

/* Color definitions (RGB565) */
#define ILI9341_BLACK   0x0000
#define ILI9341_WHITE   0xFFFF
#define ILI9341_RED     0xF800
#define ILI9341_GREEN   0x07E0
#define ILI9341_BLUE    0x001F
#define ILI9341_CYAN    0x07FF
#define ILI9341_MAGENTA 0xF81F
#define ILI9341_YELLOW  0xFFE0
#define ILI9341_ORANGE  0xFD20
#define ILI9341_GRAY    0x8410

/* Dashboard data structure */
typedef struct {
    float temperature;
    float humidity;
    bool temp_valid;
    bool humidity_valid;
    char wifi_status[16];
    char mqtt_status[16];
    char notification[64];
    uint32_t notification_time;
} ili9341_dashboard_t;

/* Font sizes */
typedef enum {
    FONT_SMALL = 0,   /* 8x8 */
    FONT_MEDIUM = 1,  /* 12x16 */
    FONT_LARGE = 2,   /* 16x24 */
} ili9341_font_t;

/**
 * Initialize ILI9341 display via SPI.
 * Uses GPIOs defined in mimi_config.h:
 *   - MIMI_TFT_MOSI_GPIO
 *   - MIMI_TFT_CLK_GPIO
 *   - MIMI_TFT_CS_GPIO
 *   - MIMI_TFT_DC_GPIO
 *   - MIMI_TFT_RST_GPIO
 *   - MIMI_TFT_BL_GPIO (optional backlight)
 *
 * @return ESP_OK on success
 */
esp_err_t ili9341_init(void);

/**
 * Check if display is initialized.
 */
bool ili9341_is_initialized(void);

/**
 * Fill entire screen with a color.
 */
esp_err_t ili9341_fill_screen(uint16_t color);

/**
 * Fill a rectangular area with a color.
 */
esp_err_t ili9341_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * Draw a single pixel.
 */
esp_err_t ili9341_draw_pixel(int x, int y, uint16_t color);

/**
 * Draw a line.
 */
esp_err_t ili9341_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

/**
 * Draw a rectangle outline.
 */
esp_err_t ili9341_draw_rect(int x, int y, int w, int h, uint16_t color);

/**
 * Draw a circle outline.
 */
esp_err_t ili9341_draw_circle(int x, int y, int r, uint16_t color);

/**
 * Draw filled circle.
 */
esp_err_t ili9341_fill_circle(int x, int y, int r, uint16_t color);

/**
 * Draw a character at position.
 */
esp_err_t ili9341_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, ili9341_font_t font);

/**
 * Draw a string at position.
 * Supports newline (\n) for multi-line text.
 */
esp_err_t ili9341_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg, ili9341_font_t font);

/**
 * Draw a string with word wrap.
 * Returns the Y position after the last line.
 */
int ili9341_draw_string_wrap(int x, int y, int max_width, const char *str, uint16_t fg, uint16_t bg, ili9341_font_t font);

/**
 * Set backlight brightness (0-100).
 * Only works if MIMI_TFT_BL_GPIO is configured.
 */
esp_err_t ili9341_set_backlight(uint8_t brightness);

/* Dashboard functions */

/**
 * Initialize dashboard UI (draw static elements).
 */
esp_err_t ili9341_dashboard_init(void);

/**
 * Update dashboard with new data.
 * Call this periodically or when data changes.
 */
esp_err_t ili9341_dashboard_update(const ili9341_dashboard_t *data);

/**
 * Show a notification message.
 * Will be displayed in notification area for a duration.
 */
esp_err_t ili9341_show_notification(const char *message, uint32_t duration_ms);

/**
 * Clear notification area.
 */
esp_err_t ili9341_clear_notification(void);