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
//	System interface for the ESP32-C6 build (derived from src/pico/i_system.c).
//

#include "pico.h"
#include "config.h"
#include <stdarg.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_log.h"
#include "doomtype.h"
#include "m_argv.h"
#include "m_misc.h"
#include "i_sound.h"
#include "i_timer.h"
#include "i_video.h"
#include "i_system.h"
#include "i_input.h"
#include "z_zone.h"

extern void I_InputInit(void);
extern void I_DisplayFrame(void);

// The zone must sit inside the 256 KB short-pointer window that starts at SHORTPTR_BASE
// (see doomtype.h). Static .bss lands there as long as the image's .bss stays small, which is
// why the framebuffers are heap-allocated in i_video.c instead of static.
#ifndef DOOM_ZONE_SIZE
#define DOOM_ZONE_SIZE (64 * 1024)   // demo peak was 26 KB in use; RP2040 quotes 45 KB worst case
#endif
static uint8_t __attribute__((aligned(4))) zone_memory[DOOM_ZONE_SIZE];

byte *I_ZoneBase(int *size)
{
    *size = DOOM_ZONE_SIZE;
    printf("zone memory: %p, %x allocated for zone\n", zone_memory, *size);
    return zone_memory;
}

typedef struct atexit_listentry_s atexit_listentry_t;
struct atexit_listentry_s {
    atexit_func_t func;
    boolean run_on_error;
    atexit_listentry_t *next;
};
static atexit_listentry_t *exit_funcs = NULL;

void I_AtExit(atexit_func_t func, boolean run_on_error)
{
    atexit_listentry_t *entry = malloc(sizeof(*entry));
    entry->func = func;
    entry->run_on_error = run_on_error;
    entry->next = exit_funcs;
    exit_funcs = entry;
}

void I_Tactile(int on, int off, int total) {}

void I_PrintBanner(const char *msg)
{
    int spaces = 35 - (strlen(msg) / 2);
    for (int i = 0; i < spaces; ++i) putchar(' ');
    puts(msg);
}

void I_PrintDivider(void)
{
    for (int i = 0; i < 75; ++i) putchar('=');
    putchar('\n');
}

void I_PrintStartupBanner(const char *gamedescription)
{
    I_PrintDivider();
    I_PrintBanner(gamedescription);
    I_PrintDivider();
    printf(" " PACKAGE_NAME " is free software, covered by the GNU General Public\n"
           " License.  There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
           " FOR A PARTICULAR PURPOSE. You are welcome to change and distribute\n"
           " copies under certain conditions. See the source for more information.\n");
    I_PrintDivider();
}

boolean I_ConsoleStdout(void) { return true; }

void I_Init(void)
{
    I_InputInit();
}

// The DOS-prompt exit screen of the RP2040 build is gone: on the medal, quitting means going
// back to the attract loop, and the cheapest correct way to do that is a clean reboot.
int8_t at_exit_screen;
uint8_t *exit_screen_kb_buffer_80;
void handle_exit_key_down(int scancode, boolean shift, uint8_t *kb_buffer, int kb_len) {}

void __attribute__((noreturn)) I_Quit(void)
{
    printf("I_Quit: restarting\n");
    fflush(stdout);
    sleep_ms(200);
    esp_restart();
    for (;;) {}
}

void I_Error(const char *error, ...)
{
    va_list argptr;
    va_start(argptr, error);
    printf("\nI_Error: ");
    vprintf(error, argptr);
    printf("\n");
    va_end(argptr);
    fflush(stdout);
    abort();
}

void *I_Realloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);
    if (size != 0 && new_ptr == NULL) {
        I_Error("I_Realloc: failed on reallocation of %" PRIuPTR " bytes", size);
    }
    return new_ptr;
}

// Read Access Violation emulation (DOS 6.22 memory dump), from PrBoom+.
static const unsigned char mem_dump_dos622[10] = { 0x57, 0x92, 0x19, 0x00, 0xF4, 0x06, 0x70, 0x00, 0x16, 0x00 };

boolean I_GetMemoryValue(unsigned int offset, void *value, int size)
{
    const unsigned char *d = mem_dump_dos622;
    switch (size) {
    case 1: *((unsigned char *)value) = d[offset]; return true;
    case 2: *((unsigned short *)value) = d[offset] | (d[offset + 1] << 8); return true;
    case 4: *((unsigned int *)value) = d[offset] | (d[offset + 1] << 8) | (d[offset + 2] << 16) | (d[offset + 3] << 24); return true;
    }
    return false;
}
