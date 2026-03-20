#include "ili9341.h"
#include "mimi_config.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ILI9341";

/* SPI device handle */
static spi_device_handle_t s_spi_dev = NULL;
static SemaphoreHandle_t s_spi_mutex = NULL;
static bool s_initialized = false;
static uint8_t s_backlight = 100;

/* Current dashboard data */
static ili9341_dashboard_t s_dashboard = {0};

/* ILI9341 commands */
#define ILI9341_NOP     0x00
#define ILI9341_SWRESET 0x01
#define ILI9341_RDDID   0x04
#define ILI9341_RDDST   0x09
#define ILI9341_SLPIN   0x10
#define ILI9341_SLPOUT  0x11
#define ILI9341_PTLON   0x12
#define ILI9341_NORON   0x13
#define ILI9341_RDMODE  0x0A
#define ILI9341_RDMADCTL 0x0B
#define ILI9341_RDPIXFMT 0x0C
#define ILI9341_RDIMGFMT 0x0D
#define ILI9341_RDSELFDIAG 0x0F
#define ILI9341_INVOFF  0x20
#define ILI9341_INVON   0x21
#define ILI9341_GAMMASET 0x26
#define ILI9341_DISPOFF 0x28
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_PASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_RAMRD   0x2E
#define ILI9341_MADCTL  0x36
#define ILI9341_PIXFMT  0x3A
#define ILI9341_FRMCTR1 0xB1
#define ILI9341_FRMCTR2 0xB2
#define ILI9341_FRMCTR3 0xB3
#define ILI9341_INVCTR  0xB4
#define ILI9341_DFUNCTR 0xB6
#define ILI9341_PWCTR1  0xC0
#define ILI9341_PWCTR2  0xC1
#define ILI9341_PWCTR3  0xC2
#define ILI9341_PWCTR4  0xC3
#define ILI9341_PWCTR5  0xC4
#define ILI9341_VMCTR1  0xC5
#define ILI9341_VMCTR2  0xC7
#define ILI9341_RDID1   0xDA
#define ILI9341_RDID2   0xDB
#define ILI9341_RDID3   0xDC
#define ILI9341_RDID4   0xDD
#define ILI9341_GMCTRP1 0xE0
#define ILI9341_GMCTRN1 0xE1

/* Transaction queue depth */
#define TRANS_QUEUE_DEPTH 8

static inline void select_cmd(void)
{
    gpio_set_level(MIMI_TFT_DC_GPIO, 0);
}

static inline void select_data(void)
{
    gpio_set_level(MIMI_TFT_DC_GPIO, 1);
}

static esp_err_t write_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    select_cmd();
    return spi_device_polling_transmit(s_spi_dev, &t);
}

static esp_err_t write_data(const uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    select_data();
    return spi_device_polling_transmit(s_spi_dev, &t);
}

static esp_err_t write_data16(uint16_t data)
{
    uint8_t buf[2] = { data >> 8, data & 0xFF };
    return write_data(buf, 2);
}

static esp_err_t set_window(int x0, int y0, int x1, int y1)
{
    esp_err_t ret;
    
    ret = write_cmd(ILI9341_CASET);
    if (ret != ESP_OK) return ret;
    ret = write_data16(x0);
    if (ret != ESP_OK) return ret;
    ret = write_data16(x1);
    if (ret != ESP_OK) return ret;
    
    ret = write_cmd(ILI9341_PASET);
    if (ret != ESP_OK) return ret;
    ret = write_data16(y0);
    if (ret != ESP_OK) return ret;
    ret = write_data16(y1);
    if (ret != ESP_OK) return ret;
    
    return write_cmd(ILI9341_RAMWR);
}

static esp_err_t hardware_reset(void)
{
#if MIMI_TFT_RST_GPIO != GPIO_NUM_NC
    gpio_set_level(MIMI_TFT_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(MIMI_TFT_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
#endif
    return ESP_OK;
}

static esp_err_t send_init_cmds(void)
{
    esp_err_t ret;
    
    ret = write_cmd(ILI9341_SWRESET);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));
    
    ret = write_cmd(ILI9341_SLPOUT);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ret = write_cmd(ILI9341_GAMMASET);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x01}, 1);
    if (ret != ESP_OK) return ret;
    
    /* Positive gamma correction */
    ret = write_cmd(ILI9341_GMCTRP1);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){
        0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1,
        0x37, 0x07, 0x10, 0x03, 0x0e, 0x06, 0x00
    }, 15);
    if (ret != ESP_OK) return ret;
    
    /* Negative gamma correction */
    ret = write_cmd(ILI9341_GMCTRN1);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){
        0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1,
        0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f
    }, 15);
    if (ret != ESP_OK) return ret;
    
    /* Memory access control: BGR order */
    ret = write_cmd(ILI9341_MADCTL);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x08}, 1);
    if (ret != ESP_OK) return ret;
    
    /* Pixel format: 16 bits/pixel */
    ret = write_cmd(ILI9341_PIXFMT);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x55}, 1);
    if (ret != ESP_OK) return ret;
    
    /* Frame rate: 70Hz */
    ret = write_cmd(ILI9341_FRMCTR1);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x00, 0x13}, 2);
    if (ret != ESP_OK) return ret;
    
    /* Display function control */
    ret = write_cmd(ILI9341_DFUNCTR);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x08, 0x82, 0x27}, 3);
    if (ret != ESP_OK) return ret;
    
    /* Power control 1 */
    ret = write_cmd(ILI9341_PWCTR1);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x17}, 1);
    if (ret != ESP_OK) return ret;
    
    /* Power control 2 */
    ret = write_cmd(ILI9341_PWCTR2);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x35}, 1);
    if (ret != ESP_OK) return ret;
    
    /* VCOM control 1 */
    ret = write_cmd(ILI9341_VMCTR1);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0x33, 0x3c}, 2);
    if (ret != ESP_OK) return ret;
    
    /* VCOM control 2 */
    ret = write_cmd(ILI9341_VMCTR2);
    if (ret != ESP_OK) return ret;
    ret = write_data((uint8_t[]){0xbe}, 1);
    if (ret != ESP_OK) return ret;
    
    /* Display on */
    ret = write_cmd(ILI9341_DISPON);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    return ESP_OK;
}

esp_err_t ili9341_init(void)
{
    if (s_initialized) return ESP_OK;
    
    esp_err_t ret;
    
    s_spi_mutex = xSemaphoreCreateMutex();
    if (!s_spi_mutex) return ESP_ERR_NO_MEM;
    
    /* Configure GPIOs */
#if MIMI_TFT_RST_GPIO != GPIO_NUM_NC
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << MIMI_TFT_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
#endif
    
    gpio_config_t dc_cfg = {
        .pin_bit_mask = 1ULL << MIMI_TFT_DC_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dc_cfg);
    
#if MIMI_TFT_BL_GPIO != GPIO_NUM_NC
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << MIMI_TFT_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(MIMI_TFT_BL_GPIO, 1);
#endif
    
    /* Initialize SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = MIMI_TFT_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = MIMI_TFT_CLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = ILI9341_WIDTH * 40 * sizeof(uint16_t),
    };
    
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Add device to SPI bus */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = MIMI_TFT_SPI_SPEED_HZ,
        .mode = 0,
        .spics_io_num = MIMI_TFT_CS_GPIO,
        .queue_size = TRANS_QUEUE_DEPTH,
        .pre_cb = NULL,
    };
    
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    /* Reset and initialize display */
    hardware_reset();
    ret = send_init_cmds();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init commands failed: %s", esp_err_to_name(ret));
        spi_bus_remove_device(s_spi_dev);
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "ILI9341 initialized: %dx%d, SPI @ %d Hz", 
             ILI9341_WIDTH, ILI9341_HEIGHT, MIMI_TFT_SPI_SPEED_HZ);
    
    return ESP_OK;
}

bool ili9341_is_initialized(void)
{
    return s_initialized;
}

esp_err_t ili9341_fill_screen(uint16_t color)
{
    return ili9341_fill_rect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

esp_err_t ili9341_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    
    esp_err_t ret = set_window(x, y, x + w - 1, y + h - 1);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_spi_mutex);
        return ret;
    }
    
    select_data();
    
    uint16_t line[w];
    for (int i = 0; i < w; i++) line[i] = color;
    
    uint8_t *line_bytes = (uint8_t *)line;
    size_t line_bytes_len = w * 2;
    
    for (int row = 0; row < h; row++) {
        spi_transaction_t t = {
            .length = line_bytes_len * 8,
            .tx_buffer = line_bytes,
        };
        ret = spi_device_polling_transmit(s_spi_dev, &t);
        if (ret != ESP_OK) break;
    }
    
    xSemaphoreGive(s_spi_mutex);
    return ret;
}

esp_err_t ili9341_draw_pixel(int x, int y, uint16_t color)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (x < 0 || x >= ILI9341_WIDTH || y < 0 || y >= ILI9341_HEIGHT) 
        return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    
    esp_err_t ret = set_window(x, y, x, y);
    if (ret == ESP_OK) {
        ret = write_data16(color);
    }
    
    xSemaphoreGive(s_spi_mutex);
    return ret;
}

esp_err_t ili9341_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        ili9341_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
    
    return ESP_OK;
}

esp_err_t ili9341_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    ili9341_draw_line(x, y, x + w - 1, y, color);
    ili9341_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    ili9341_draw_line(x, y, x, y + h - 1, color);
    ili9341_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    return ESP_OK;
}

esp_err_t ili9341_draw_circle(int x, int y, int r, uint16_t color)
{
    int px = -r, py = 0, err = 2 - 2 * r;
    do {
        ili9341_draw_pixel(x - px, y + py, color);
        ili9341_draw_pixel(x - py, y - px, color);
        ili9341_draw_pixel(x + px, y - py, color);
        ili9341_draw_pixel(x + py, y + px, color);
        r = err;
        if (r <= py) err += ++py * 2 + 1;
        if (r > px || err > py) err += ++px * 2 + 1;
    } while (px < 0);
    return ESP_OK;
}

esp_err_t ili9341_fill_circle(int x, int y, int r, uint16_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r * r - dy * dy);
        ili9341_draw_line(x - dx, y + dy, x + dx, y + dy, color);
    }
    return ESP_OK;
}

/* 8x8 font (simplified ASCII) */
static const uint8_t font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00}, /* ! */
    {0x6c,0x6c,0x24,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x6c,0xfe,0x6c,0x6c,0xfe,0x6c,0x00,0x00}, /* # */
    {0x18,0x3e,0x58,0x3c,0x1a,0x7c,0x18,0x00}, /* $ */
    {0x00,0xc6,0xcc,0x18,0x30,0x66,0xc6,0x00}, /* % */
    {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, /* & */
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, /* ( */
    {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00}, /* ) */
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, /* * */
    {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* , */
    {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x02,0x06,0x0c,0x18,0x30,0x60,0xc0,0x00}, /* / */
    {0x7c,0xc6,0xce,0xd6,0xe6,0xc6,0x7c,0x00}, /* 0 */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, /* 1 */
    {0x7c,0xc6,0x06,0x1c,0x30,0x66,0xfe,0x00}, /* 2 */
    {0x7c,0xc6,0x06,0x3c,0x06,0xc6,0x7c,0x00}, /* 3 */
    {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00}, /* 4 */
    {0xfe,0xc0,0xc0,0xfc,0x06,0xc6,0x7c,0x00}, /* 5 */
    {0x38,0x60,0xc0,0xfc,0xc6,0xc6,0x7c,0x00}, /* 6 */
    {0xfe,0xc6,0x0c,0x18,0x30,0x30,0x30,0x00}, /* 7 */
    {0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0x7c,0x00}, /* 8 */
    {0x7c,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0x00}, /* 9 */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, /* : */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, /* ; */
    {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00}, /* < */
    {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, /* = */
    {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00}, /* > */
    {0x7c,0xc6,0x0c,0x18,0x18,0x00,0x18,0x00}, /* ? */
    {0x7c,0xc6,0xde,0xde,0xde,0xc0,0x78,0x00}, /* @ */
    {0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0x00}, /* A */
    {0xfc,0x66,0x66,0x7c,0x66,0x66,0xfc,0x00}, /* B */
    {0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0x00}, /* C */
    {0xf8,0x6c,0x66,0x66,0x66,0x6c,0xf8,0x00}, /* D */
    {0xfe,0x62,0x68,0x78,0x68,0x62,0xfe,0x00}, /* E */
    {0xfe,0x62,0x68,0x78,0x68,0x60,0xf0,0x00}, /* F */
    {0x3c,0x66,0xc0,0xc0,0xce,0x66,0x3e,0x00}, /* G */
    {0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0x00}, /* H */
    {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, /* I */
    {0x1e,0x0c,0x0c,0x0c,0xcc,0xcc,0x78,0x00}, /* J */
    {0xe6,0x66,0x6c,0x78,0x6c,0x66,0xe6,0x00}, /* K */
    {0xf0,0x60,0x60,0x60,0x62,0x66,0xfe,0x00}, /* L */
    {0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0x00}, /* M */
    {0xc6,0xe6,0xf6,0xde,0xce,0xc6,0xc6,0x00}, /* N */
    {0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00}, /* O */
    {0xfc,0x66,0x66,0x7c,0x60,0x60,0xf0,0x00}, /* P */
    {0x7c,0xc6,0xc6,0xc6,0xc6,0xce,0x7c,0x0e}, /* Q */
    {0xfc,0x66,0x66,0x7c,0x6c,0x66,0xe6,0x00}, /* R */
    {0x7c,0xc6,0xe0,0x7c,0x06,0xc6,0x7c,0x00}, /* S */
    {0x7e,0x5a,0x18,0x18,0x18,0x18,0x3c,0x00}, /* T */
    {0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00}, /* U */
    {0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x00}, /* V */
    {0xc6,0xc6,0xc6,0xd6,0xfe,0xee,0xc6,0x00}, /* W */
    {0xc6,0xc6,0x6c,0x38,0x6c,0xc6,0xc6,0x00}, /* X */
    {0x66,0x66,0x66,0x3c,0x18,0x18,0x3c,0x00}, /* Y */
    {0xfe,0xc6,0x8c,0x18,0x32,0x66,0xfe,0x00}, /* Z */
    {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00}, /* [ */
    {0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00}, /* \ */
    {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00}, /* ] */
    {0x00,0x18,0x3c,0x7e,0x18,0x18,0x18,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff}, /* _ */
    {0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x78,0x0c,0x7c,0xcc,0x76,0x00}, /* a */
    {0xe0,0x60,0x7c,0x66,0x66,0x66,0xdc,0x00}, /* b */
    {0x00,0x00,0x7c,0xc6,0xc0,0xc6,0x7c,0x00}, /* c */
    {0x1c,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0x00}, /* d */
    {0x00,0x00,0x7c,0xc6,0xfe,0xc0,0x7c,0x00}, /* e */
    {0x38,0x6c,0x60,0xf8,0x60,0x60,0xf0,0x00}, /* f */
    {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0xf8}, /* g */
    {0xe0,0x60,0x6c,0x76,0x66,0x66,0xe6,0x00}, /* h */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00}, /* i */
    {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3c}, /* j */
    {0xe0,0x60,0x66,0x6c,0x78,0x6c,0xe6,0x00}, /* k */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, /* l */
    {0x00,0x00,0xec,0xfe,0xd6,0xd6,0xd6,0x00}, /* m */
    {0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x00}, /* n */
    {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7c,0x00}, /* o */
    {0x00,0x00,0xdc,0x66,0x66,0x7c,0x60,0xf0}, /* p */
    {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0x1e}, /* q */
    {0x00,0x00,0xdc,0x76,0x60,0x60,0xf0,0x00}, /* r */
    {0x00,0x00,0x7c,0xc0,0x7c,0x06,0xfc,0x00}, /* s */
    {0x30,0x30,0xfc,0x30,0x30,0x36,0x1c,0x00}, /* t */
    {0x00,0x00,0xcc,0xcc,0xcc,0xcc,0x76,0x00}, /* u */
    {0x00,0x00,0xc6,0xc6,0xc6,0x6c,0x38,0x00}, /* v */
    {0x00,0x00,0xc6,0xd6,0xd6,0xfe,0x6c,0x00}, /* w */
    {0x00,0x00,0xc6,0x6c,0x38,0x6c,0xc6,0x00}, /* x */
    {0x00,0x00,0xc6,0xc6,0xc6,0x7e,0x06,0xfc}, /* y */
    {0x00,0x00,0x7c,0x18,0x38,0x60,0x7c,0x00}, /* z */
};

esp_err_t ili9341_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, ili9341_font_t font)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (c < ' ' || c > '~') c = '?';
    
    int idx = c - ' ';
    int size = 1 << font;
    
    for (int row = 0; row < 8; row++) {
        uint8_t line = font8x8[idx][row];
        for (int col = 0; col < 8; col++) {
            uint16_t color = (line & (1 << (7 - col))) ? fg : bg;
            for (int dy = 0; dy < size; dy++) {
                for (int dx = 0; dx < size; dx++) {
                    ili9341_draw_pixel(x + col * size + dx, y + row * size + dy, color);
                }
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t ili9341_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg, ili9341_font_t font)
{
    if (!s_initialized || !str) return ESP_ERR_INVALID_STATE;
    
    int size = 1 << font;
    int char_w = 8 * size;
    int char_h = 8 * size;
    int cx = x, cy = y;
    
    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy += char_h;
        } else {
            ili9341_draw_char(cx, cy, *str, fg, bg, font);
            cx += char_w;
        }
        str++;
    }
    
    return ESP_OK;
}

int ili9341_draw_string_wrap(int x, int y, int max_width, const char *str, uint16_t fg, uint16_t bg, ili9341_font_t font)
{
    if (!s_initialized || !str) return y;
    
    int size = 1 << font;
    int char_w = 8 * size;
    int char_h = 8 * size;
    int chars_per_line = max_width / char_w;
    int cx = x, cy = y;
    int char_count = 0;
    
    while (*str) {
        if (*str == '\n' || char_count >= chars_per_line) {
            cx = x;
            cy += char_h;
            char_count = 0;
            if (*str == '\n') { str++; continue; }
        }
        ili9341_draw_char(cx, cy, *str, fg, bg, font);
        cx += char_w;
        char_count++;
        str++;
    }
    
    return cy + char_h;
}

esp_err_t ili9341_set_backlight(uint8_t brightness)
{
#if MIMI_TFT_BL_GPIO != GPIO_NUM_NC
    if (brightness > 100) brightness = 100;
    s_backlight = brightness;
    
    if (brightness > 50) {
        gpio_set_level(MIMI_TFT_BL_GPIO, 1);
    } else {
        gpio_set_level(MIMI_TFT_BL_GPIO, 0);
    }
    
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* Dashboard implementation */

#define DASH_TITLE_Y        10
#define DASH_TEMP_Y         60
#define DASH_HUMID_Y        120
#define DASH_STATUS_Y       180
#define DASH_NOTIF_Y        260

static void draw_header(void)
{
    ili9341_fill_rect(0, 0, ILI9341_WIDTH, 40, ILI9341_BLUE);
    ili9341_draw_string(10, DASH_TITLE_Y - 8, "MimiClaw Dashboard", ILI9341_WHITE, ILI9341_BLUE, FONT_MEDIUM);
}

static void draw_temp_area(void)
{
    ili9341_draw_rect(10, DASH_TEMP_Y - 5, 220, 50, ILI9341_GRAY);
    ili9341_draw_string(20, DASH_TEMP_Y, "Temperature:", ILI9341_WHITE, ILI9341_BLACK, FONT_SMALL);
}

static void draw_humid_area(void)
{
    ili9341_draw_rect(10, DASH_HUMID_Y - 5, 220, 50, ILI9341_GRAY);
    ili9341_draw_string(20, DASH_HUMID_Y, "Humidity:", ILI9341_WHITE, ILI9341_BLACK, FONT_SMALL);
}

static void draw_status_area(void)
{
    ili9341_draw_rect(10, DASH_STATUS_Y - 5, 220, 70, ILI9341_GRAY);
    ili9341_draw_string(20, DASH_STATUS_Y, "Status:", ILI9341_WHITE, ILI9341_BLACK, FONT_SMALL);
}

static void draw_notif_area(void)
{
    ili9341_draw_rect(10, DASH_NOTIF_Y - 5, 220, 50, ILI9341_ORANGE);
}

esp_err_t ili9341_dashboard_init(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    ili9341_fill_screen(ILI9341_BLACK);
    draw_header();
    draw_temp_area();
    draw_humid_area();
    draw_status_area();
    draw_notif_area();
    
    memset(&s_dashboard, 0, sizeof(s_dashboard));
    strcpy(s_dashboard.wifi_status, "N/A");
    strcpy(s_dashboard.mqtt_status, "N/A");
    
    return ESP_OK;
}

esp_err_t ili9341_dashboard_update(const ili9341_dashboard_t *data)
{
    if (!s_initialized || !data) return ESP_ERR_INVALID_STATE;
    
    char buf[32];
    
    /* Update temperature */
    if (data->temp_valid) {
        snprintf(buf, sizeof(buf), "%.1f C", data->temperature);
        ili9341_fill_rect(20, DASH_TEMP_Y + 20, 200, 20, ILI9341_BLACK);
        ili9341_draw_string(20, DASH_TEMP_Y + 20, buf, ILI9341_YELLOW, ILI9341_BLACK, FONT_LARGE);
        s_dashboard.temperature = data->temperature;
        s_dashboard.temp_valid = true;
    }
    
    /* Update humidity */
    if (data->humidity_valid) {
        snprintf(buf, sizeof(buf), "%.1f %%", data->humidity);
        ili9341_fill_rect(20, DASH_HUMID_Y + 20, 200, 20, ILI9341_BLACK);
        ili9341_draw_string(20, DASH_HUMID_Y + 20, buf, ILI9341_CYAN, ILI9341_BLACK, FONT_LARGE);
        s_dashboard.humidity = data->humidity;
        s_dashboard.humidity_valid = true;
    }
    
    /* Update status */
    if (data->wifi_status[0]) {
        snprintf(buf, sizeof(buf), "WiFi: %s", data->wifi_status);
        ili9341_fill_rect(20, DASH_STATUS_Y + 15, 200, 16, ILI9341_BLACK);
        ili9341_draw_string(20, DASH_STATUS_Y + 15, buf, ILI9341_WHITE, ILI9341_BLACK, FONT_SMALL);
        strncpy(s_dashboard.wifi_status, data->wifi_status, sizeof(s_dashboard.wifi_status) - 1);
    }
    
    if (data->mqtt_status[0]) {
        snprintf(buf, sizeof(buf), "MQTT: %s", data->mqtt_status);
        ili9341_fill_rect(20, DASH_STATUS_Y + 35, 200, 16, ILI9341_BLACK);
        ili9341_draw_string(20, DASH_STATUS_Y + 35, buf, ILI9341_WHITE, ILI9341_BLACK, FONT_SMALL);
        strncpy(s_dashboard.mqtt_status, data->mqtt_status, sizeof(s_dashboard.mqtt_status) - 1);
    }
    
    return ESP_OK;
}

esp_err_t ili9341_show_notification(const char *message, uint32_t duration_ms)
{
    if (!s_initialized || !message) return ESP_ERR_INVALID_STATE;
    
    ili9341_fill_rect(15, DASH_NOTIF_Y, 210, 40, ILI9341_ORANGE);
    ili9341_draw_string_wrap(20, DASH_NOTIF_Y + 5, 200, message, ILI9341_BLACK, ILI9341_ORANGE, FONT_SMALL);
    
    strncpy(s_dashboard.notification, message, sizeof(s_dashboard.notification) - 1);
    s_dashboard.notification_time = (uint32_t)(esp_timer_get_time() / 1000);
    
    (void)duration_ms;  /* Auto-clear handled by caller */
    
    return ESP_OK;
}

esp_err_t ili9341_clear_notification(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    ili9341_fill_rect(15, DASH_NOTIF_Y, 210, 40, ILI9341_BLACK);
    s_dashboard.notification[0] = '\0';
    
    return ESP_OK;
}