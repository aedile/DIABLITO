/* pico/audio_i2s.h shim: the pico-extras audio buffer-pool API that i_picosound.c's mixer
 * talks to. Implemented in esp/i_sound_glue.c (phase 3: paced sink that discards; phase 5: I2S). */
#pragma once
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { AUDIO_BUFFER_FORMAT_PCM_S16 = 1 };
struct audio_format { uint32_t sample_freq; uint16_t format; uint16_t channel_count; };
struct audio_buffer_format { const struct audio_format *format; uint sample_stride; };
typedef struct mem_buffer { size_t size; uint8_t *bytes; } mem_buffer_t;
typedef struct audio_buffer {
    mem_buffer_t *buffer;
    const struct audio_buffer_format *format;
    uint sample_count;
    uint max_sample_count;
    struct audio_buffer *next;
} audio_buffer_t;
struct audio_buffer_pool;
struct audio_i2s_config { uint8_t data_pin, clock_pin_base, dma_channel, pio_sm; };

struct audio_buffer_pool *audio_new_producer_pool(struct audio_buffer_format *format, int buffer_count, int buffer_sample_count);
audio_buffer_t *take_audio_buffer(struct audio_buffer_pool *pool, bool block);
void give_audio_buffer(struct audio_buffer_pool *pool, audio_buffer_t *buffer);
const struct audio_format *audio_i2s_setup(const struct audio_format *intended, const struct audio_i2s_config *config);
bool audio_i2s_connect_extra(struct audio_buffer_pool *producer, bool buffer_on_give, uint buffer_count, uint samples_per_buffer, void *format);
void audio_i2s_set_enabled(bool enabled);
#ifdef __cplusplus
}
#endif
