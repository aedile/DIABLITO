// Sound glue for the ESP32-C6 build.
//
// The RP2040 mixer (src/pico/i_picosound.c) is kept verbatim; it talks to pico-extras' audio
// buffer-pool API, implemented here on the ES8311 codec (I2C control) and I2S DMA output, both
// lifted from NESTOR/PELLETINO. Pacing is DAC-driven: a buffer is handed to the mixer only when
// the DMA ring has room for it, so production is locked to the I2S clock and never blocks.
//
// Music: the OPL2 emulator (opl/) is the mixer's music generator. It runs at half the chip's
// native rate (24,858 Hz, pitch and envelopes corrected at the register level in opl_pico.c), so
// that is the I2S rate; the sound effects are resampled to it by the mixer.
#include "pico.h"
#include "pico/audio_i2s.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "doomtype.h"
#include "i_sound.h"

static const char *TAG = "AUDIO";

/* Waveshare ESP32-C6-LCD-1.69 (see NOTES/hardware-map.md) */
#define PIN_I2S_MCK  GPIO_NUM_19
#define PIN_I2S_BCK  GPIO_NUM_20
#define PIN_I2S_LRCK GPIO_NUM_22
#define PIN_I2S_DOUT GPIO_NUM_23
#define PIN_I2S_DIN  GPIO_NUM_21
#define PIN_I2C_SDA  GPIO_NUM_8
#define PIN_I2C_SCL  GPIO_NUM_7
#define ES8311_ADDR  0x18

#define DMA_FRAMES_PER_DESC 256
#define DMA_DESCS 8                      /* 2048 stereo frames = 82 ms at 24858 Hz */

static i2s_chan_handle_t tx;
static volatile uint32_t bytes_sent;     /* advanced by the DMA ISR */
static uint32_t bytes_written;
static volatile uint32_t underruns;
static size_t ring_bytes;
static uint32_t frame_bytes = 4;         /* stereo int16 */
static uint32_t sample_rate = 49716;

/* ---- ES8311 (register sequence proven on this board in PELLETINO) ---- */
static esp_err_t es8311_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return i2c_master_write_to_device(I2C_NUM_0, ES8311_ADDR, data, 2, pdMS_TO_TICKS(100));
}

static void es8311_init(void)
{
    i2c_config_t c = {
        .mode = I2C_MODE_MASTER, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_0, &c);
    esp_err_t r = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) ESP_LOGE(TAG, "i2c install: %s", esp_err_to_name(r));

    es8311_write_reg(0x00, 0x3F); vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(0x00, 0x00); vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(0x01, 0x3F); es8311_write_reg(0x02, 0x00); es8311_write_reg(0x03, 0x10);
    es8311_write_reg(0x04, 0x10); es8311_write_reg(0x05, 0x00); es8311_write_reg(0x06, 0x03);
    es8311_write_reg(0x07, 0x00); es8311_write_reg(0x08, 0xFF);
    es8311_write_reg(0x09, 0x0C);   /* SDP out: 16-bit, Philips I2S */
    es8311_write_reg(0x0A, 0x0C);   /* SDP in */
    es8311_write_reg(0x0D, 0x00);
    es8311_write_reg(0x0E, 0x02); es8311_write_reg(0x0F, 0x44);
    es8311_write_reg(0x10, 0x0C); es8311_write_reg(0x11, 0x00);
    es8311_write_reg(0x12, 0x00); es8311_write_reg(0x13, 0x10); es8311_write_reg(0x14, 0x10);
    es8311_write_reg(0x32, 0xBF);   /* DAC volume */
    es8311_write_reg(0x17, 0xBF);   /* ADC volume */
    es8311_write_reg(0x00, 0x80);
    es8311_write_reg(0x01, 0x3F);
}

static bool IRAM_ATTR on_sent(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx) { bytes_sent += e->size; return false; }
static bool IRAM_ATTR on_underrun(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx) { underruns++; return false; }

/* ---- pico audio pool API ---- */
struct audio_buffer_pool {
    audio_buffer_t *buffers;
    int count, samples, next;
};

struct audio_buffer_pool *audio_new_producer_pool(struct audio_buffer_format *format, int buffer_count, int buffer_sample_count)
{
    struct audio_buffer_pool *p = calloc(1, sizeof *p);
    p->count = buffer_count;
    p->samples = buffer_sample_count;
    p->buffers = calloc(buffer_count, sizeof(audio_buffer_t));
    for (int i = 0; i < buffer_count; i++) {
        mem_buffer_t *m = calloc(1, sizeof *m);
        m->size = buffer_sample_count * format->sample_stride;
        m->bytes = malloc(m->size);
        p->buffers[i].buffer = m;
        p->buffers[i].format = format;
        p->buffers[i].max_sample_count = buffer_sample_count;
        p->buffers[i].sample_count = buffer_sample_count;
    }
    return p;
}

/* Samples the driver has not accepted yet. They are written before anything new is mixed, so
 * every sample mixed is played, in order (PELLETINO's scheme); a dropped tail is a tick in the music. */
static const uint8_t *pending_ptr;
static size_t pending_len;

static bool flush_pending(void)
{
    while (pending_len) {
        size_t written = 0;
        i2s_channel_write(tx, pending_ptr, pending_len, &written, 0);
        bytes_written += written;
        pending_ptr += written; pending_len -= written;
        if (!written) return false;          /* ring full: try again on the next update */
    }
    return true;
}

/* The mixer gets a buffer only when nothing is pending and the DMA ring (less the descriptor the
 * driver keeps in flight) can take all of it: production is locked to the DAC clock, the write
 * never blocks, and latency is bounded by the ring (82 ms). */
audio_buffer_t *take_audio_buffer(struct audio_buffer_pool *pool, bool block)
{
    (void)block;
    if (!tx || !flush_pending()) return NULL;
    int32_t queued = (int32_t)(bytes_written - bytes_sent);
    if (queued < 0) { bytes_written = bytes_sent; queued = 0; }     /* ring ran dry: resync */
    if ((size_t)queued + pool->samples * frame_bytes > ring_bytes - DMA_FRAMES_PER_DESC * frame_bytes) return NULL;
    audio_buffer_t *b = &pool->buffers[pool->next];
    pool->next = (pool->next + 1) % pool->count;
    return b;
}

static bool muted;
void audio_set_mute(bool m) { muted = m; }
bool audio_is_muted(void) { return muted; }

void give_audio_buffer(struct audio_buffer_pool *pool, audio_buffer_t *buffer)
{
    (void)pool;
    size_t bytes = buffer->sample_count * frame_bytes;
    if (muted) memset(buffer->buffer->bytes, 0, bytes);   /* same number of samples, so the DAC still paces the mixer */
    pending_ptr = buffer->buffer->bytes; pending_len = bytes;
    flush_pending();
}

const struct audio_format *audio_i2s_setup(const struct audio_format *intended, const struct audio_i2s_config *config)
{
    (void)config;
    es8311_init();
    frame_bytes = 2 * intended->channel_count;
    sample_rate = intended->sample_freq;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = DMA_DESCS;
    chan.dma_frame_num = DMA_FRAMES_PER_DESC;
    chan.auto_clear = true;              /* silence, not a stale buffer, on underrun */
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, NULL));
    i2s_event_callbacks_t cbs = { .on_sent = on_sent, .on_send_q_ovf = on_underrun };
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx, &cbs, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(intended->sample_freq),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                        intended->channel_count == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = PIN_I2S_MCK, .bclk = PIN_I2S_BCK, .ws = PIN_I2S_LRCK, .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ring_bytes = (size_t)DMA_DESCS * DMA_FRAMES_PER_DESC * frame_bytes;
    ESP_LOGI(TAG, "ES8311 + I2S: %lu Hz, %u ch, ring %u bytes (%lu ms)", (unsigned long)intended->sample_freq,
             intended->channel_count, (unsigned)ring_bytes, (unsigned long)(ring_bytes / frame_bytes * 1000 / intended->sample_freq));
    return intended;
}

bool audio_i2s_connect_extra(struct audio_buffer_pool *producer, bool buffer_on_give, uint buffer_count, uint samples_per_buffer, void *format)
{
    (void)producer; (void)buffer_on_give; (void)buffer_count; (void)samples_per_buffer; (void)format;
    return true;
}

void audio_i2s_set_enabled(bool enabled)
{
    if (!tx) return;
    if (enabled) {
        static const int16_t silence[DMA_FRAMES_PER_DESC * 2 * 2];   /* two descriptors of stereo silence */
        size_t loaded = 0;
        i2s_channel_preload_data(tx, silence, sizeof silence, &loaded);
        bytes_written = loaded;
        ESP_ERROR_CHECK(i2s_channel_enable(tx));
    } else {
        i2s_channel_disable(tx);
    }
}

uint32_t audio_underrun_count(void) { return underruns; }
int audio_queued_ms(void) { return tx ? (int)((int32_t)(bytes_written - bytes_sent) / (int32_t)frame_bytes * 1000 / (int32_t)sample_rate) : 0; }
