/* display.c - ST7789 over raw spi_master with two DMA strip buffers. See display.h. */
#include "display.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DISPLAY";

#define DMA_BUFFER_SIZE (DISPLAY_WIDTH * STRIP_ROWS * 2)
#define ST7789_OFFSET 20   /* the visible 280 columns sit in the middle of the panel's 320-long axis */

#define ST7789_SWRESET 0x01
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_NORON   0x13
#define ST7789_INVON   0x21
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3A

static spi_device_handle_t spi;
static uint8_t *dma_buffer[2];
static spi_transaction_t trans[2];
static int cur;
static bool pending;
static int vx, vy, vw = DISPLAY_WIDTH, vh = DISPLAY_HEIGHT;
uint32_t display_wait_us;

static void pre_cb(spi_transaction_t *t) { gpio_set_level(PIN_LCD_DC, (int)t->user); }

static void send_cmd(uint8_t cmd)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd, .user = (void *)0 };
    spi_device_polling_transmit(spi, &t);
}

static void send_data(const uint8_t *data, size_t len)
{
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data, .user = (void *)1 };
    spi_device_polling_transmit(spi, &t);
}

void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x0 = x + ST7789_OFFSET, x1 = x + w - 1 + ST7789_OFFSET, y1 = y + h - 1;
    uint8_t c[4] = { x0 >> 8, x0, x1 >> 8, x1 }, r[4] = { y >> 8, y, y1 >> 8, y1 };
    send_cmd(ST7789_CASET); send_data(c, 4);
    send_cmd(ST7789_RASET); send_data(r, 4);
    send_cmd(ST7789_RAMWR);
}

void display_wait_done(void)
{
    if (!pending) return;
    spi_transaction_t *r;
    int64_t t0 = esp_timer_get_time();
    spi_device_get_trans_result(spi, &r, portMAX_DELAY);
    display_wait_us += esp_timer_get_time() - t0;
    pending = false;
}

static void queue_strip(size_t bytes)
{
    trans[cur].length = bytes * 8;
    trans[cur].rxlength = 0;
    trans[cur].tx_buffer = dma_buffer[cur];
    trans[cur].rx_buffer = NULL;
    trans[cur].user = (void *)1;
    spi_device_queue_trans(spi, &trans[cur], portMAX_DELAY);
    pending = true;
    cur ^= 1;
}

void display_fill(uint16_t color)
{
    uint16_t sw = (color >> 8) | (color << 8);
    display_wait_done();
    display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    size_t chunk = DMA_BUFFER_SIZE / 2;
    uint16_t *b = (uint16_t *)dma_buffer[0];
    for (size_t i = 0; i < chunk; i++) b[i] = sw;
    size_t left = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    while (left) {
        size_t n = left > chunk ? chunk : left;
        spi_transaction_t t = { .length = n * 16, .tx_buffer = dma_buffer[0], .user = (void *)1 };
        spi_device_transmit(spi, &t);
        left -= n;
    }
}

void display_set_viewport(int x, int y, int w, int h) { vx = x; vy = y; vw = w; vh = h; }

uint16_t *display_acquire_strip(void) { return (uint16_t *)dma_buffer[cur]; }

void display_submit_strip(int y0, int nrows)
{
    if (y0 == 0) {
        display_wait_done();                 /* commands are polling transfers: the queue must be empty */
        display_set_window(vx, vy, vw, vh);
    }
    display_wait_done();                     /* the other strip must be off the wire before we reuse it next time */
    queue_strip((size_t)vw * nrows * 2);
}

IRAM_ATTR void display_push_strip(const uint8_t *rows, int pitch, int y0, int nrows, const uint16_t *pal)
{
    /* Convert into the free buffer while the other strip is on the wire: two pixels per 32-bit
     * store, eight per iteration. rows must be 4-byte aligned. */
    uint32_t *dst = (uint32_t *)dma_buffer[cur];
    const int words = vw / 4;
    for (int r = 0; r < nrows; r++) {
        const uint32_t *src = (const uint32_t *)(rows + r * pitch);
        for (int x = 0; x < words; x += 2) {
            uint32_t a = src[x], b = src[x + 1];
            dst[0] = pal[a & 0xFF] | (uint32_t)pal[(a >> 8) & 0xFF] << 16;
            dst[1] = pal[(a >> 16) & 0xFF] | (uint32_t)pal[a >> 24] << 16;
            dst[2] = pal[b & 0xFF] | (uint32_t)pal[(b >> 8) & 0xFF] << 16;
            dst[3] = pal[(b >> 16) & 0xFF] | (uint32_t)pal[b >> 24] << 16;
            dst += 4;
        }
    }
    display_submit_strip(y0, nrows);
}

void display_sleep(bool sleep)
{
    display_wait_done();
    if (sleep) {
        display_set_backlight(0);
        send_cmd(ST7789_SLPIN);
    } else {
        send_cmd(ST7789_SLPOUT);
        vTaskDelay(pdMS_TO_TICKS(120));
        display_set_backlight(153);
    }
}

void display_set_backlight(uint8_t brightness)
{
    static bool ready;
    if (!ready) {
        ledc_timer_config_t tc = { .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
                                   .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
        ledc_timer_config(&tc);
        ledc_channel_config_t cc = { .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                                     .timer_sel = LEDC_TIMER_0, .intr_type = LEDC_INTR_DISABLE,
                                     .gpio_num = PIN_LCD_BL, .duty = 0, .hpoint = 0 };
        ledc_channel_config(&cc);
        ready = true;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_init(void)
{
    gpio_config_t io = { .pin_bit_mask = (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus = { .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_LCD_SCLK,
                             .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = DMA_BUFFER_SIZE };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = { .clock_speed_hz = LCD_SPI_CLOCK, .mode = 0, .spics_io_num = PIN_LCD_CS,
                                          .queue_size = 7, .pre_cb = pre_cb };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &spi));

    dma_buffer[0] = heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA);
    dma_buffer[1] = heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA);
    assert(dma_buffer[0] && dma_buffer[1]);

    send_cmd(ST7789_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    send_cmd(ST7789_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(120));
    uint8_t colmod = 0x55, madctl = 0x60;   /* RGB565; MX|MV = landscape, USB connector on the left */
    send_cmd(ST7789_COLMOD); send_data(&colmod, 1);
    send_cmd(ST7789_MADCTL); send_data(&madctl, 1);
    send_cmd(ST7789_INVON);
    send_cmd(ST7789_NORON);  vTaskDelay(pdMS_TO_TICKS(10));
    send_cmd(ST7789_DISPON); vTaskDelay(pdMS_TO_TICKS(10));
    display_fill(0x0000);
    display_set_backlight(153);
    ESP_LOGI(TAG, "ST7789 up: 280x240 landscape, SPI %d MHz, 2 x %d B DMA strips", LCD_SPI_CLOCK / 1000000, DMA_BUFFER_SIZE);
}
