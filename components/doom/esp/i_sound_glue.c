// Sound glue for the ESP32-C6 build.
//
// The RP2040 mixer (src/pico/i_picosound.c) is kept verbatim; it talks to pico-extras' audio
// buffer-pool API, which is implemented here. Phase 3: the pool hands out one buffer every
// buffer-period so sound effects advance at real-time speed, and give_audio_buffer() just drops
// the samples. Phase 5 replaces the sink with the ES8311/I2S path.
//
// Also stubs the OPL music module (music is a stretch goal, see NOTES/disabled-and-stubbed.md).
#include "pico.h"
#include "pico/audio_i2s.h"
#include "esp_timer.h"
#include "doomtype.h"
#include "i_sound.h"

struct audio_buffer_pool {
    audio_buffer_t *buffers;
    int count, samples;
    uint32_t sample_freq;
    int64_t next_take_us;
};

struct audio_buffer_pool *audio_new_producer_pool(struct audio_buffer_format *format, int buffer_count, int buffer_sample_count)
{
    struct audio_buffer_pool *p = calloc(1, sizeof *p);
    p->count = buffer_count;
    p->samples = buffer_sample_count;
    p->sample_freq = format->format->sample_freq;
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

audio_buffer_t *take_audio_buffer(struct audio_buffer_pool *pool, bool block)
{
    (void)block;
    int64_t now = esp_timer_get_time();
    if (now < pool->next_take_us) return NULL;
    int64_t period = (int64_t)pool->samples * 1000000 / pool->sample_freq;
    pool->next_take_us = (pool->next_take_us > now - 2 * period ? pool->next_take_us : now) + period;
    return &pool->buffers[0];
}

void give_audio_buffer(struct audio_buffer_pool *pool, audio_buffer_t *buffer) { (void)pool; (void)buffer; }

const struct audio_format *audio_i2s_setup(const struct audio_format *intended, const struct audio_i2s_config *config)
{
    (void)config;
    return intended;
}

bool audio_i2s_connect_extra(struct audio_buffer_pool *producer, bool buffer_on_give, uint buffer_count, uint samples_per_buffer, void *format)
{
    (void)producer; (void)buffer_on_give; (void)buffer_count; (void)samples_per_buffer; (void)format;
    return true;
}

void audio_i2s_set_enabled(bool enabled) { (void)enabled; }

// ---- music: OPL emulation not built ----
uint8_t restart_song_state;   // owned by i_oplmusic.c when music is compiled in

static snddevice_t music_none_devices[] = { SNDDEVICE_NONE };
static boolean Music_Init(void) { return false; }
static void Music_Shutdown(void) {}
static void Music_SetVolume(int volume) { (void)volume; }
static void Music_Pause(void) {}
static void Music_Resume(void) {}
static void *Music_RegisterSong(should_be_const void *data, int len) { (void)data; (void)len; return NULL; }
static void Music_UnRegisterSong(void *handle) { (void)handle; }
static void Music_PlaySong(void *handle, boolean looping) { (void)handle; (void)looping; }
static void Music_StopSong(void) {}
static boolean Music_IsPlaying(void) { return false; }
static void Music_Poll(void) {}

const music_module_t music_opl_module = {
    music_none_devices,
    0,
    Music_Init,
    Music_Shutdown,
    Music_SetVolume,
    Music_Pause,
    Music_Resume,
    Music_RegisterSong,
    Music_UnRegisterSong,
    Music_PlaySong,
    Music_StopSong,
    Music_IsPlaying,
    Music_Poll,
};

void I_SetOPLDriverVer(opl_driver_ver_t ver) { (void)ver; }
