/*
 * display.h - ST7789 240x280 driver, landscape 280x240, raw spi_master + DMA.
 * Lifted from NESTOR/components/display (which came from PELLETINO) and trimmed to what
 * DIABLITO uses: a viewport that frames are streamed into as 16-row strips.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH   280
#define DISPLAY_HEIGHT  240
#define STRIP_ROWS      16

/* GPIO (Waveshare ESP32-C6-LCD-1.69, see NOTES/hardware-map.md) */
#define PIN_LCD_MOSI    GPIO_NUM_2
#define PIN_LCD_SCLK    GPIO_NUM_1
#define PIN_LCD_CS      GPIO_NUM_5
#define PIN_LCD_DC      GPIO_NUM_3
#define PIN_LCD_RST     GPIO_NUM_4
#define PIN_LCD_BL      GPIO_NUM_6
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_CLOCK   80000000

void display_init(void);                                   /* panel up, landscape, black, backlight 60 % */
void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void display_fill(uint16_t rgb565);                        /* whole panel, blocking */
void display_set_backlight(uint8_t brightness);            /* 0..255 */
void display_sleep(bool sleep);                            /* backlight off + panel SLPIN, and back; caller must not be streaming */

/* Frames are streamed into a viewport (default: whole panel). w must be a multiple of 4. */
void display_set_viewport(int x, int y, int w, int h);

/* Indexed path: nrows rows of viewport-width 8-bit pixels at rows (pitch bytes apart), pal is 256
 * RGB565 entries already byte-swapped for the panel. y0 == 0 starts a new frame (sets the
 * window). Converts into the free DMA strip while the other is on the wire; returns once queued. */
void display_push_strip(const uint8_t *rows, int pitch, int y0, int nrows, const uint16_t *pal);

/* Zero-copy path: write pre-swapped RGB565 straight into the free strip (viewport w x STRIP_ROWS
 * pixels), then submit it. Same y0 == 0 rule. */
uint16_t *display_acquire_strip(void);
void display_submit_strip(int y0, int nrows);

void display_wait_done(void);                              /* block until the queued strip has gone out */
extern uint32_t display_wait_us;                           /* accumulated time blocked in strip waits (profiling) */

#ifdef __cplusplus
}
#endif
