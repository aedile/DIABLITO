//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM graphics stuff for Pico.
//

#if PICODOOM_RENDER_NEWHOPE
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <doom/r_data.h>
#include "doom/f_wipe.h"
#include "pico.h"

#include "config.h"
#include "d_loop.h"
#include "deh_str.h"
#include "doomtype.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_diskicon.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "pico/time.h"
#include "picodoom.h"
#include "image_decoder.h"
#include "display.h"
#include "doom/doomstat.h"
#include "i_sound.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if SUPPORT_TEXT
typedef struct __packed {
    const char * const name;
    const uint8_t * const data;
    const uint8_t w;
    const uint8_t h;
} txt_font_t;
#define TXT_SCREEN_W 80
#include "fonts/normal.h"


static uint16_t ega_colors[] = {
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0x00, 0x00),         // 0: Black
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0x00, 0xa8),         // 1: Blue
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0xa8, 0x00),         // 2: Green
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0xa8, 0xa8),         // 3: Cyan
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0x00, 0x00),         // 4: Red
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0x00, 0xa8),         // 5: Magenta
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0x54, 0x00),         // 6: Brown
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0xa8, 0xa8),         // 7: Grey
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0x54, 0x54),         // 8: Dark grey
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0x54, 0xfe),         // 9: Bright blue
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0xfe, 0x54),         // 10: Bright green
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0xfe, 0xfe),         // 11: Bright cyan
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0x54, 0x54),         // 12: Bright red
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0x54, 0xfe),         // 13: Bright magenta
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0xfe, 0x54),         // 14: Yellow
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0xfe, 0xfe),         // 15: Bright white
};
#endif

// todo temproarly turned this off because it causes a seeming bug in scanvideo (perhaps only with the new callback stuff) where the last repeated scanline of a pixel line is freed while shown
//  note it may just be that this happens anyway, but usually we are writing slower than the beam?
//  previously PICO_ON_DEVICE but disabled to save scratch_x space
#define USE_INTERP 0
#if USE_INTERP
#include "hardware/interp.h"
#endif

CU_REGISTER_DEBUG_PINS(scanline_copy)
//CU_SELECT_DEBUG_PINS(scanline_copy)

static const patch_t *stbar;

volatile uint8_t interp_in_use;

// display has been set up?

static boolean initialized = false;

static uint32_t frame = 0;

boolean screenvisible = true;

//int vga_porch_flash = false;

//static int startup_delay = 1000;

// The screen buffer; this is modified to draw things to the screen
//pixel_t *I_VideoBuffer = NULL;
// Gamma correction level to use

boolean screensaver_mode = false;

isb_int8_t usegamma = 0;

// Joystick/gamepad hysteresis
unsigned int joywait = 0;

pixel_t *I_VideoBuffer; // todo can't have this

uint8_t *frame_buffer[2];   // one contiguous heap block, see I_InitGraphics
static uint16_t palette[256];
static uint16_t __scratch_x("shared_pal") shared_pal[NUM_SHARED_PALETTES][16]; // TODO I can't fit all this in scratch but it's probably going to affect performance
static int8_t next_pal=-1;

semaphore_t render_frame_ready, display_frame_freed;

uint8_t *text_screen_data;
static uint16_t *text_scanline_buffer_start;
static uint8_t *text_screen_cpy;
static uint8_t *text_font_cpy;

#if USE_INTERP
static interp_hw_save_t interp0_save, interp1_save;
static boolean interp_updated;
static boolean need_save;

static inline void interp_save_static(interp_hw_t *interp, interp_hw_save_t *saver) {
    saver->accum[0] = interp->accum[0];
    saver->accum[1] = interp->accum[1];
    saver->base[0] = interp->base[0];
    saver->base[1] = interp->base[1];
    saver->base[2] = interp->base[2];
    saver->ctrl[0] = interp->ctrl[0];
    saver->ctrl[1] = interp->ctrl[1];
}

static inline void interp_restore_static(interp_hw_t *interp, interp_hw_save_t *saver) {
    interp->accum[0] = saver->accum[0];
    interp->accum[1] = saver->accum[1];
    interp->base[0] = saver->base[0];
    interp->base[1] = saver->base[1];
    interp->base[2] = saver->base[2];
    interp->ctrl[0] = saver->ctrl[0];
    interp->ctrl[1] = saver->ctrl[1];
}
#endif

void I_ShutdownGraphics(void)
{
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?
}

//
// Set the window title
//

void I_SetWindowTitle(const char *title)
{
//    window_title = title;
}

//
// I_SetPalette
//
void I_SetPaletteNum(int doompalette)
{
    next_pal = doompalette;
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
}

uint8_t display_frame_index;
uint8_t display_overlay_index;
uint8_t display_video_type;

typedef void (*scanline_func)(uint16_t *dest, int scanline);

static void scanline_func_none(uint16_t *dest, int scanline);
static void scanline_func_double(uint16_t *dest, int scanline);
static void scanline_func_single(uint16_t *dest, int scanline);
static void scanline_func_wipe(uint16_t *dest, int scanline);

uint8_t *wipe_yoffsets; // position of start of y in each column
int16_t *wipe_yoffsets_raw;
uint32_t *wipe_linelookup; // offset of each line from start of screenbuffer (can be negative for FB 1 to FB 0)
uint8_t next_video_type;
uint8_t next_frame_index; // todo combine with video type?
uint8_t next_overlay_index;
#if !DEMO1_ONLY
uint8_t *next_video_scroll;
uint8_t *video_scroll;
#endif
volatile uint8_t wipe_min;

#pragma GCC push_options
#if PICO_ON_DEVICE
#pragma GCC optimize("O3")
#endif

static inline void palette_convert_scanline(uint16_t *dest, const uint8_t *src) {
#if USE_INTERP
    if (interp_updated != 1) {
                if (need_save) {
                    interp_save_static(interp0, &interp0_save);
                    interp_save_static(interp1, &interp1_save);
                }
                interp_config c = interp_default_config();
                interp_config_set_shift(&c, 0);
                interp_config_set_mask(&c, 0, 7);
                interp_set_config(interp0, 0, &c);
                interp_config_set_shift(&c, 16);
                interp_set_config(interp1, 0, &c);
                interp_config_set_shift(&c, 8);
                interp_config_set_cross_input(&c, true);
                interp_set_config(interp0, 1, &c);
                interp_config_set_shift(&c, 24);
                interp_set_config(interp1, 1, &c);
                uint32_t palette_div2 = ((uintptr_t)palette) >> 1;
                interp0->base[0] = palette_div2;
                interp0->base[1] = palette_div2;
                interp1->base[0] = palette_div2;
                interp1->base[1] = palette_div2;
                interp_updated = 1;
            }
            extern void palette8to16(uint32_t *dest, const uint8_t *src, uint words);
            palette8to16(dest, src, SCREENWIDTH);
//            dest[4] = (255-scanline) * 0x2000;
            dest += SCREENWIDTH / 2;
//            dest[-4] = (255-scanline) * 0x10001;
#else
    for (int i = 0; i < SCREENWIDTH; i++) {
        uint16_t val = palette[*src++];;
        *dest++ = val;
    }
#endif
}

static void scanline_func_none(uint16_t *dest, int scanline) {
    memset(dest, 0, SCREENWIDTH * 2);
}

#if SUPPORT_TEXT
void check_text_buffer(uint16_t *buffer) {
#if PICO_ON_DEVICE
    if (buffer < text_scanline_buffer_start || buffer >= text_scanline_buffer_start + TEXT_SCANLINE_BUFFER_TOTAL_WORDS) {
        // is an original scanvideo allocated buffer, we need to use a larger one
        int i;
        for(i=0;i<PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT;i++) {
            if (!saved_scanline_buffer_ptrs[i]) break;
        }
        assert(i<PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT);
        saved_scanline_buffer_ptrs[i] = buffer;
        buffer = text_scanline_buffer_start + i * TEXT_SCANLINE_BUFFER_WORDS;
    }
#endif
}

static void __not_in_flash_func(render_text_mode_half_scanline)(uint16_t *buffer, const uint8_t *text_data, int yoffset) {
    uint16_t * p = (uint16_t *)(buffer);
//    memset(buffer->data + 1, 0, 1280);
//    uint x = scanline * 2 + yoffset;
//    buffer->data[1 + (x/2)] = x&1 ? 0xffff0000 : 0xffff;
#if 1
    uint blink = frame & 16;
    // not going to change so just hard code
//    assert(normal_font.w == 8);
//    assert(normal_font.h == 16);
    const uint8_t *font_base = text_font_cpy + yoffset;
    for(uint i=0;i<80;i++) {
        uint fg = text_data[1] & 0xf;
        uint bg = (text_data[1] >> 4) & 0xf;
        if (bg & 0x8) {
            bg &= ~0x8;
            // blinking
            if (blink) fg = bg;
        }
        // probably user error but this wasn't working correctly with the inline asm on the stack
        static uint16_t colors[2];
        colors[0] = ega_colors[bg];
        colors[1] = ega_colors[fg];
        uint bits8 = font_base[text_data[0] * 16];
#if PICO_ON_DEVICE
        // todo use interpolator?
        uint tmp1, tmp2, tmp3;
        __asm__ volatile (
            ".syntax unified\n"

            "movs %[r_tmp3], #2\n"
            "lsls %[r_tmp1],%[r_bits8],#1\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "movs %[r_tmp2],%[r_bits8]\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

            "lsrs %[r_tmp1],%[r_bits8],#1\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "lsrs %[r_tmp2],%[r_bits8],#2\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

            "lsrs %[r_tmp1],%[r_bits8],#3\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "lsrs %[r_tmp2],%[r_bits8],#4\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

            "lsrs %[r_tmp1],%[r_bits8],#5\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "lsrs %[r_tmp2],%[r_bits8],#6\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

        : [ r_p] "+l" (p),
              [ r_tmp1] "=&l" (tmp1),
              [ r_tmp2] "=&l" (tmp2),
              [ r_tmp3] "=&l" (tmp3)

            : [ r_bits8] "l" (bits8),
              [ r_colors] "l" (colors)
            :
        );
#else
        p[0] = colors[bits8&1];
        p[1] = colors[(bits8>>1)&1];
        p[2] = colors[(bits8>>2)&1];
        p[3] = colors[(bits8>>3)&1];
        p[4] = colors[(bits8>>4)&1];
        p[5] = colors[(bits8>>5)&1];
        p[6] = colors[(bits8>>6)&1];
        p[7] = colors[(bits8>>7)&1];
        p+=8;
#endif
        text_data+=2;
    }
#endif
}

static void __noinline render_text_mode_scanline(uint16_t *buffer, int scanline) {
    const uint8_t *text_data = text_screen_data;
    assert(text_data);
    text_data += TXT_SCREEN_W * 2 * (scanline/8);
    check_text_buffer(buffer);
    render_text_mode_half_scanline(buffer, text_data, (scanline & 7u)*2 );
}
#endif

static void scanline_func_double(uint16_t *dest, int scanline) {
    const uint8_t* src = frame_buffer[display_frame_index] + scanline * SCREENWIDTH;

    if (scanline < MAIN_VIEWHEIGHT) { 
        // for (int x = 0; x < DOOM_WIDTH; x++) {
            // const uint8_t* source_color = src + x;
            // uint16_t palette_color = palette[*source_color];
            // dest[x] = palette_color;
        // }
        palette_convert_scanline(dest, src);
    } else {
        // we expect everything to be overdrawn by statusbar so we do nothing
    }
}

static void __not_in_flash_func(scanline_func_single)(uint16_t *dest, int scanline) {
    uint8_t *src;
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index] + scanline * SCREENWIDTH;
    } else {
        src = frame_buffer[display_frame_index^1] + (scanline - 32) * SCREENWIDTH;
    }
#if !DEMO1_ONLY
    if (video_scroll) {
        for(int i=SCREENWIDTH-1;i>0;i--) {
            src[i] = src[i-1];
        }
        src[0] = video_scroll[scanline];
    }
#endif
    palette_convert_scanline(dest, src);
}

static void scanline_func_wipe(uint16_t *dest, int scanline) {
    const uint8_t *src;
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index];
    } else {
        src = frame_buffer[display_frame_index^1] - 32 * SCREENWIDTH;
    }
    assert(wipe_yoffsets && wipe_linelookup);
    uint16_t *d = (uint16_t *)dest;
    src += scanline * SCREENWIDTH;
    for (int i = 0; i < SCREENWIDTH; i++) {
        int rel = scanline - wipe_yoffsets[i];
        if (rel < 0) {
            d[i] = palette[src[i]];
            // st7789_put(rgb_to_bgr(palette[src[i]]));
            // dest[i] = rgb_to_bgr(palette[src[i]]);
        } else {
            const uint8_t *flip;
#if PICO_ON_DEVICE
            flip = (const uint8_t *)wipe_linelookup[rel];
#else
            flip = &frame_buffer[0][0] + wipe_linelookup[rel];
#endif
            // todo better protection here
            if (flip >= &frame_buffer[0][0] && flip < &frame_buffer[0][0] + 2 * SCREENWIDTH * MAIN_VIEWHEIGHT) {
                d[i] = palette[flip[i]];
                // st7789_put(rgb_to_bgr(palette[flip[i]]));
                // dest[i] = rgb_to_bgr(palette[src[i]]);
            }
        }
    }
}

static inline uint draw_vpatch(uint16_t *dest, patch_t *patch, vpatchlist_t *vp, uint off) {
    int repeat = vp->entry.repeat;
    dest += vp->entry.x;
    int w = vpatch_width(patch);
    const uint8_t *data0 = vpatch_data(patch);
    const uint8_t *data = data0 + off;
    if (!vpatch_has_shared_palette(patch)) {
        const uint8_t *pal = vpatch_palette(patch);
        switch (vpatch_type(patch)) {
            case vp4_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 1; i < len; i += 2) {
                        uint v = *data++;
                        *p++ = palette[pal[v & 0xf]];
                        *p++ = palette[pal[v >> 4]];
                    }
                    if (len & 1) {
                        *p++ = palette[pal[(*data++) & 0xf]];
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp4_alpha: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = palette[pal[v & 0xf]];
                    if (v >> 4) p[1] = palette[pal[v >> 4]];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = palette[pal[v & 0xf]];
                }
                break;
            }
            case vp4_solid: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    p[0] = palette[pal[v & 0xf]];
                    p[1] = palette[pal[v >> 4]];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    p[0] = palette[pal[v & 0xf]];
                }
                break;
            }
            case vp6_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 3; i < len; i += 4) {
                        uint v = *data++;
                        v |= (*data++) << 8;
                        v |= (*data++) << 16;
                        *p++ = palette[pal[v & 0x3f]];
                        *p++ = palette[pal[(v >> 6) & 0x3f]];
                        *p++ = palette[pal[(v >> 12) & 0x3f]];
                        *p++ = palette[pal[(v >> 18) & 0x3f]];
                    }
                    len &= 3;
                    if (len--) {
                        uint v = *data++;
                        *p++ = palette[pal[v & 0x3f]];
                        if (len--) {
                            v >>= 6;
                            v |= (*data++) << 2;
                            *p++ = palette[pal[v & 0x3f]];
                            if (len--) {
                                v >>= 6;
                                v |= (*data++) << 4;
                                *p++ = palette[pal[v & 0x3f]];
                                assert(!len);
                            }
                        }
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp8_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 0; i < len; i++) {
                        *p++ = palette[pal[*data++]];
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp_border: {
                dest[0] = palette[*data++];
                uint16_t col = palette[*data++];
                for (int i = 1; i < w - 1; i++) dest[i] = col;
                dest[w-1] = palette[*data++];
                break;
            }
            default:
                assert(false);
                break;
        }
    } else {
        uint sp = vpatch_shared_palette(patch);
        uint16_t *pal16 = shared_pal[sp];
        assert(sp < NUM_SHARED_PALETTES);
        switch (vpatch_type(patch)) {
            case vp4_solid: {
                if (((uintptr_t)dest)&3) {
                    uint16_t *p = dest;
                    for (int i = 0; i < w / 2; i++) {
                        uint v = *data++;
                        p[0] = pal16[v & 0xf];
                        p[1] = pal16[v >> 4];
                        p += 2;
                    }
                } else {
                    uint32_t *wide = (uint32_t *) dest;
                    for (int i = 0; i < w / 2; i++) {
                        uint v = *data++;
                        wide[i] = pal16[v & 0xf] | (pal16[v >> 4] << 16);
                    }
                }
                if (w & 1) {
                    uint v = *data++;
                    dest[w-1] = pal16[v & 0xf];
                }
                break;
            }
            case vp4_alpha: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = pal16[v & 0xf];
                    if (v >> 4) p[1] = pal16[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = pal16[v & 0xf];
                }
                break;
            }
            default:
                assert(false);
        }
    }
    if (repeat) {
        // we need them to be solid... which they are, but if not you'll just get some visual funk
        //assert(vpatch_type(patch) == vp4_solid);
        if (vp->entry.patch_handle == VPATCH_M_THERMM) w--; // hackity hack
        for(int i=0;i<repeat*w;i++) {
            dest[w+i] = dest[i];
        }
    }
    return data - data0;
}

// this is not in flash as quite large and only once per frame
void __noinline new_frame_init_overlays_palette_and_wipe() {
    // re-initialize our overlay drawing
    if (display_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS) {
        memset(vpatchlists->vpatch_next, 0, sizeof(vpatchlists->vpatch_next));
        memset(vpatchlists->vpatch_starters, 0, sizeof(vpatchlists->vpatch_starters));
        memset(vpatchlists->vpatch_doff, 0, sizeof(vpatchlists->vpatch_doff));
        vpatchlist_t *overlays = vpatchlists->overlays[display_overlay_index];
        // do it in reverse so our linked lists are in ascending order
        for (int i = overlays->header.size - 1; i > 0; i--) {
            assert(overlays[i].entry.y < count_of(vpatchlists->vpatch_starters));
            vpatchlists->vpatch_next[i] = vpatchlists->vpatch_starters[overlays[i].entry.y];
            vpatchlists->vpatch_starters[overlays[i].entry.y] = i;
        }
        if (next_pal != -1) {
            static const uint8_t *playpal;
            static bool calculate_palettes;
            if (!playpal) {
                lumpindex_t l = W_GetNumForName("PLAYPAL");
                playpal = W_CacheLumpNum(l, PU_STATIC);
                calculate_palettes = W_LumpLength(l) == 768;
            }
            if (!calculate_palettes || !next_pal) {
                const uint8_t *doompalette = playpal + next_pal * 768;
                for (int i = 0; i < 256; i++) {
                    int r = *doompalette++;
                    int g = *doompalette++;
                    int b = *doompalette++;
                    if (usegamma) {
                        r = gammatable[usegamma-1][r];
                        g = gammatable[usegamma-1][g];
                        b = gammatable[usegamma-1][b];
                    }
                    palette[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b);
                }
            } else {
                int mul, r0, g0, b0;
                if (next_pal < 9) {
                    mul = next_pal * 65536 / 9;
                    r0 = 255; g0 = b0 = 0;
                } else if (next_pal < 13) {
                    mul = (next_pal - 8) * 65536 / 8;
                    r0 = 215; g0 = 186; b0 = 69;
                } else {
                    mul = 65536 / 8;
                    r0 = b0 = 0; g0 = 256;
                }
                const uint8_t *doompalette = playpal;
                for (int i = 0; i < 256; i++) {
                    int r = *doompalette++;
                    int g = *doompalette++;
                    int b = *doompalette++;
                    r += ((r0 - r) * mul) >> 16;
                    g += ((g0 - g) * mul) >> 16;
                    b += ((b0 - b) * mul) >> 16;
                    palette[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b);
                }
            }
            next_pal = -1;
            assert(vpatch_type(stbar) == vp4_solid); // no transparent, no runs, 4 bpp
            for (int i = 0; i < NUM_SHARED_PALETTES; i++) {
                patch_t *patch = resolve_vpatch_handle(vpatch_for_shared_palette[i]);
                assert(vpatch_colorcount(patch) <= 16);
                assert(vpatch_has_shared_palette(patch));
                for (int j = 0; j < 16; j++) {
                    shared_pal[i][j] = palette[vpatch_palette(patch)[j]];
                }
            }
        }
        if (display_video_type == VIDEO_TYPE_WIPE) {
//            printf("WIPEMIN %d\n", wipe_min);
            if (wipe_min <= 200) {
                bool regular = display_overlay_index; // just happens to toggle every frame
                int new_wipe_min = 200;
                for (int i = 0; i < SCREENWIDTH; i++) {
                    int v;
                    if (wipe_yoffsets_raw[i] < 0) {
                        if (regular) {
                            wipe_yoffsets_raw[i]++;
                        }
                        v = 0;
                    } else {
                        int dy = (wipe_yoffsets_raw[i] < 16) ? (1 + wipe_yoffsets_raw[i] + regular) / 2 : 4;
                        if (wipe_yoffsets_raw[i] + dy > 200) {
                            v = 200;
                        } else {
                            wipe_yoffsets_raw[i] += dy;
                            v = wipe_yoffsets_raw[i];
                        }
                    }
                    wipe_yoffsets[i] = v;
                    if (v < new_wipe_min) new_wipe_min = v;
                }
                assert(new_wipe_min >= wipe_min);
                wipe_min = new_wipe_min;
            }
        }
    }
}

// this method moved out of scratchx because we didn't have quite enough space for core1 stack
void __no_inline_not_in_flash_func(new_frame_stuff)() {
    if (sem_available(&render_frame_ready)) {
        sem_acquire_blocking(&render_frame_ready);
        display_video_type = next_video_type;
        display_frame_index = next_frame_index;
        display_overlay_index = next_overlay_index;
#if !DEMO1_ONLY
        video_scroll = next_video_scroll; // todo does this waste too much space
#endif
        sem_release(&display_frame_freed);
    } else {
#if !DEMO1_ONLY
        video_scroll = NULL;
#endif
    }
    if (display_video_type != VIDEO_TYPE_SAVING) {
        // this stuff is large (so in flash) and not needed in save move
        new_frame_init_overlays_palette_and_wipe();
    }
}

// this is the code that sorts the vpatches and passes them to draw_vpatch, which directly draws the relevant pixels of the overlay into the buffer
void __scratch_x("scanlines") handle_overlays(uint16_t *buffer, int scanline) {
    assert(scanline < count_of(vpatchlists->vpatch_starters));
    int prev = 0;
    for (int vp = vpatchlists->vpatch_starters[scanline]; vp;) {
        int next = vpatchlists->vpatch_next[vp];
        while (vpatchlists->vpatch_next[prev] && vpatchlists->vpatch_next[prev] < vp) {
            prev = vpatchlists->vpatch_next[prev];
        }
        assert(prev != vp);
        assert(vpatchlists->vpatch_next[prev] != vp);
        vpatchlists->vpatch_next[vp] = vpatchlists->vpatch_next[prev];
        vpatchlists->vpatch_next[prev] = vp;
        prev = vp;
        vp = next;
    }
    vpatchlist_t *overlays = vpatchlists->overlays[display_overlay_index];
    prev = 0;
    for (int vp = vpatchlists->vpatch_next[prev]; vp; vp = vpatchlists->vpatch_next[prev]) {
        patch_t *patch = resolve_vpatch_handle(overlays[vp].entry.patch_handle);
        int yoff = scanline - overlays[vp].entry.y;
        if (yoff < vpatch_height(patch)) {
            vpatchlists->vpatch_doff[vp] = draw_vpatch((uint16_t*)(buffer), patch, &overlays[vp],
                                                        vpatchlists->vpatch_doff[vp]);
            prev = vp;
        } else {
            vpatchlists->vpatch_next[prev] = vpatchlists->vpatch_next[vp];
        }
    }
}

// 320 -> 280 wide: keep seven of every eight pixels. One load/store per output pixel, no table.
static inline void downsample_line(const uint16_t *s, uint16_t *d) {
    for (int g = 0; g < SCREENWIDTH / 8; g++, s += 8, d += 7) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; d[4] = s[4]; d[5] = s[5]; d[6] = s[6];
    }
}

#define DOOM_VIEW_W 280
#define DOOM_VIEW_Y ((DISPLAY_HEIGHT - SCREENHEIGHT) / 2)

// Fast path for a 3D-view row with no overlay on it: palette-convert and 8:7 downsample in one
// pass, four source pixels per load, straight into the DMA strip. No 320-wide intermediate.
static inline void convert_downsample(const uint8_t *src, uint16_t *d) {
    const uint32_t *s = (const uint32_t *)src;
    for (int g = 0; g < SCREENWIDTH / 8; g++, s += 2, d += 7) {
        uint32_t a = s[0], b = s[1];
        d[0] = palette[a & 0xff]; d[1] = palette[(a >> 8) & 0xff]; d[2] = palette[(a >> 16) & 0xff]; d[3] = palette[a >> 24];
        d[4] = palette[b & 0xff]; d[5] = palette[(b >> 8) & 0xff]; d[6] = palette[(b >> 16) & 0xff];
    }
}

// Compose every scanline (palette + overlays) exactly as the RP2040 did ahead of the beam, but
// into 16-row DMA strips: strip N+1 is composed while strip N is on the SPI wire.
void fill_scanlines() {
    frame++;
    uint16_t line[SCREENWIDTH];
    uint16_t *strip = display_acquire_strip();
    int row = 0, y0 = 0;
    for (int scanline = 0; scanline < SCREENHEIGHT; scanline++) {
        if (display_video_type == VIDEO_TYPE_DOUBLE && scanline < MAIN_VIEWHEIGHT &&
            !vpatchlists->vpatch_next[0] && !vpatchlists->vpatch_starters[scanline]) {
            // nothing overlaid on this row (no menu open): the overlay walker would be a no-op
            convert_downsample(frame_buffer[display_frame_index] + scanline * SCREENWIDTH, strip + row * DOOM_VIEW_W);
            goto next_row;
        }
        switch (display_video_type) {
            case VIDEO_TYPE_SINGLE: scanline_func_single(line, scanline); break;
            case VIDEO_TYPE_DOUBLE: scanline_func_double(line, scanline); break;
            case VIDEO_TYPE_WIPE:   scanline_func_wipe(line, scanline); break;
            default:                scanline_func_none(line, scanline); break;
        }
        if (display_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS) {
            handle_overlays(line, scanline);
        }
        downsample_line(line, strip + row * DOOM_VIEW_W);
    next_row:
        if (++row == STRIP_ROWS || scanline == SCREENHEIGHT - 1) {
            display_submit_strip(y0, row);
            y0 += row;
            row = 0;
            strip = display_acquire_strip();
        }
    }
}

// The display task: what core 1 did on the RP2040. Blocks until the game hands over a frame,
// flips the display state, hands the previous framebuffer back, then composes and streams the
// strips, sleeping on the SPI DMA between them so the game task renders the next frame meanwhile.
// Logs frame time per frame: the phase 3 gate asks for it.
uint32_t doom_frame_count;
static void display_task(void *arg)
{
    int64_t last_us = 0;
    for (;;) {
        sem_acquire_blocking(&render_frame_ready);
        display_video_type = next_video_type;
        display_frame_index = next_frame_index;
        display_overlay_index = next_overlay_index;
#if !DEMO1_ONLY
        video_scroll = next_video_scroll;
#endif
        sem_release(&display_frame_freed);
        if (display_video_type != VIDEO_TYPE_SAVING) {
            new_frame_init_overlays_palette_and_wipe();
        }
        int64_t t0 = esp_timer_get_time();
        fill_scanlines();
        int64_t t1 = esp_timer_get_time();
        doom_frame_count++;
        if (last_us) {
            printf("frame %lu: %ld us total, %ld us display, type %d\n", (unsigned long)doom_frame_count,
                   (long)(t1 - last_us), (long)(t1 - t0), display_video_type);
        }
        last_us = t1;
#if DOOM_PROFILE
        if ((doom_frame_count & 511) == 0) { extern void prof_report(void); prof_report(); }
#endif
        if ((doom_frame_count & 255) == 0) {
            extern int Z_FreeMemory(void);
            extern uint32_t audio_underrun_count(void); extern int audio_queued_ms(void);
            printf("audio: %lu underruns, %d ms queued\n", (unsigned long)audio_underrun_count(), audio_queued_ms());
            printf("mem @frame %lu: heap free %u, largest %u, min ever %u, zone free %d, display stack free %u, uptime %lu s, gamestate %d, demo %d, usergame %d, tic %d\n",
                   (unsigned long)doom_frame_count, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), Z_FreeMemory(),
                   (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                   (unsigned long)(t1 / 1000000), (int)gamestate, (int)demoplayback, (int)usergame, (int)gametic);
        }
    }
}

void I_DisplayFrame(void) {}   // kept for callers; the display task picks frames up by itself
#pragma GCC pop_options

void I_InitGraphics(void)
{
    frame_buffer[0] = heap_caps_malloc(2 * SCREENWIDTH * MAIN_VIEWHEIGHT, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    assert(frame_buffer[0]);
    frame_buffer[1] = frame_buffer[0] + SCREENWIDTH * MAIN_VIEWHEIGHT;
    memset(frame_buffer[0], 0, 2 * SCREENWIDTH * MAIN_VIEWHEIGHT);
    display_set_viewport(0, DOOM_VIEW_Y, DOOM_VIEW_W, SCREENHEIGHT);

    stbar = resolve_vpatch_handle(VPATCH_STBAR);
    sem_init(&render_frame_ready, 0, 2);
    sem_init(&display_frame_freed, 1, 2);
    pd_init();
    // above the game task (1) so a finished frame starts going out at once, below BLE (5)
    xTaskCreate(display_task, "display", 6144, NULL, 3, NULL);
    initialized = true;
}

// Bind all variables controlling video options into the configuration
// file system.
void I_BindVideoVariables(void)
{
//    M_BindIntVariable("use_mouse",                 &usemouse);
//    M_BindIntVariable("fullscreen",                &fullscreen);
//    M_BindIntVariable("video_display",             &video_display);
//    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
//    M_BindIntVariable("integer_scaling",           &integer_scaling);
//    M_BindIntVariable("vga_porch_flash",           &vga_porch_flash);
//    M_BindIntVariable("startup_delay",             &startup_delay);
//    M_BindIntVariable("fullscreen_width",          &fullscreen_width);
//    M_BindIntVariable("fullscreen_height",         &fullscreen_height);
//    M_BindIntVariable("force_software_renderer",   &force_software_renderer);
//    M_BindIntVariable("max_scaling_buffer_pixels", &max_scaling_buffer_pixels);
//    M_BindIntVariable("window_width",              &window_width);
//    M_BindIntVariable("window_height",             &window_height);
//    M_BindIntVariable("grabmouse",                 &grabmouse);
//    M_BindStringVariable("video_driver",           &video_driver);
//    M_BindStringVariable("window_position",        &window_position);
//    M_BindIntVariable("usegamma",                  &usegamma);
//    M_BindIntVariable("png_screenshots",           &png_screenshots);
}

//
// I_StartTic
//
void I_StartTic (void)
{
    if (!initialized)
    {
        return;
    }

    I_GetEvent();
//
//    if (usemouse && !nomouse && window_focused)
//    {
//        I_ReadMouse();
//    }
//
//    if (joywait < I_GetTime())
//    {
//        I_UpdateJoystick();
//    }
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

int I_GetPaletteIndex(int r, int g, int b)
{
    return 0;
}

#if !NO_USE_ENDDOOM
void I_Endoom(byte *endoom_data) {
    uint32_t size;
    uint8_t *wa = pd_get_work_area(&size);
    assert(size >=TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 + 80*25*2 + 4096);
    text_screen_cpy = wa;
    text_font_cpy = text_screen_cpy + 80 * 25 * 2;
    text_scanline_buffer_start = (uint16_t *) (text_font_cpy + 4096 * 2); // BOB I changed this from 32 to 16 and mul'ed the 4096 by 2
#if 0
    static_assert(sizeof(normal_font_data) == 4096, "");
    memcpy(text_font_cpy, normal_font_data, sizeof(normal_font_data));
    memcpy(text_screen_cpy, endoom_data, 80 * 25 * 2);
#else
    static_assert(TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 > 1024 + 512, "");
    uint8_t *tmp_buf = (uint8_t *)text_scanline_buffer_start;
    uint16_t *decoder = (uint16_t *)(tmp_buf + 512);
    th_bit_input bi;
    th_bit_input_init(&bi, normal_font_data_z);
    decode_data(text_font_cpy, 4096, &bi, decoder, 512, tmp_buf, 512);
    th_bit_input_init(&bi, endoom_data);
    // text
    decode_data(text_screen_cpy, 80*25, &bi, decoder, 512, tmp_buf, 512);
    // attr
    decode_data(text_screen_cpy+80*25, 80*25, &bi, decoder, 512, tmp_buf, 512);
    static_assert(TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 > 80*25*2, "");
    // re-interlace the text & attr
    memcpy(tmp_buf, text_screen_cpy, 80*25*2);
    for(int i=0;i<80*25;i++) {
        text_screen_cpy[i*2] = tmp_buf[i];
        text_screen_cpy[i*2+1] = tmp_buf[80*25 + i];
    }
#endif
    text_screen_data = text_screen_cpy;
}
#endif

void I_GraphicsCheckCommandLine(void)
{
//    int i;
//
//    //!
//    // @category video
//    // @vanilla
//    //
//    // Disable blitting the screen.
//    //
//
//    noblit = M_CheckParm ("-noblit");
//
//    //!
//    // @category video
//    //
//    // Don't grab the mouse when running in windowed mode.
//    //
//
//    nograbmouse_override = M_ParmExists("-nograbmouse");
//
//    // default to fullscreen mode, allow override with command line
//    // nofullscreen because we love prboom
//
//    //!
//    // @category video
//    //
//    // Run in a window.
//    //
//
//    if (M_CheckParm("-window") || M_CheckParm("-nofullscreen"))
//    {
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    //
//    // Run in fullscreen mode.
//    //
//
//    if (M_CheckParm("-fullscreen"))
//    {
//        fullscreen = true;
//    }
//
//    //!
//    // @category video
//    //
//    // Disable the mouse.
//    //
//
//    nomouse = M_CheckParm("-nomouse") > 0;
//
//    //!
//    // @category video
//    // @arg <x>
//    //
//    // Specify the screen width, in pixels. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-width", 1);
//
//    if (i > 0)
//    {
//        window_width = atoi(myargv[i + 1]);
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    // @arg <y>
//    //
//    // Specify the screen height, in pixels. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-height", 1);
//
//    if (i > 0)
//    {
//        window_height = atoi(myargv[i + 1]);
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    // @arg <WxY>
//    //
//    // Specify the dimensions of the window. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-geometry", 1);
//
//    if (i > 0)
//    {
//        int w, h, s;
//
//        s = sscanf(myargv[i + 1], "%ix%i", &w, &h);
//        if (s == 2)
//        {
//            window_width = w;
//            window_height = h;
//            fullscreen = false;
//        }
//    }
//
//    //!
//    // @category video
//    //
//    // Don't scale up the screen. Implies -window.
//    //
//
//    if (M_CheckParm("-1"))
//    {
//        SetScaleFactor(1);
//    }
//
//    //!
//    // @category video
//    //
//    // Double up the screen to 2x its normal size. Implies -window.
//    //
//
//    if (M_CheckParm("-2"))
//    {
//        SetScaleFactor(2);
//    }
//
//    //!
//    // @category video
//    //
//    // Double up the screen to 3x its normal size. Implies -window.
//    //
//
//    if (M_CheckParm("-3"))
//    {
//        SetScaleFactor(3);
//    }
}

// Check if we have been invoked as a screensaver by xscreensaver.

void I_CheckIsScreensaver(void)
{
}

void I_DisplayFPSDots(boolean dots_on)
{
}

#endif