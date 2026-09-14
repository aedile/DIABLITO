# Phase 3: First light

Date: 2026-09-14. Engine compiled for the C6 and ran the title screen, wipe, and the built-in
demo loop on the first flash. Logs: `NOTES/logs/phase3-run1.log` (first run, 40 s),
`NOTES/logs/phase3-soak-10min.log` (the ten-minute gate run).

## What the port consists of

`components/doom/`:

- `src/` is a copy of upstream `kilograham/rp2040-doom` `src/` (minus heretic, hexen, strife,
  setup, whd_gen, fonts). Patched files, all small:
  - `doomtype.h`: `SHORTPTR_BASE 0x40800000` on ESP; the ARM `bkpt` removed from the
    short-pointer range check.
  - `m_fixed.h`: the Cortex-M0+ inline-asm `FixedMulInline` is skipped on ESP (RV32IM has a
    hardware multiplier; the C `(int64)a*b >> 16` compiles to `mul`+`mulh`).
  - `w_file_memory.c`: the WHD base is a runtime pointer set by `W_Memory_SetBase()` instead of
    the link-time `TINY_WAD_ADDR`.
  - `pd_render.cpp`: `USE_CORE1_FOR_FLATS/REGULAR` and `USE_INTERP` overridable and set to 0;
    `vpatchlists` and `list_buffer` come from the heap; `frame_buffer` is a pointer pair; the
    three busy-waits that used to let core 1 catch up call `pump_display()`; `pd_end_frame()`
    ends with `I_DisplayFrame()`.
  - `doom/p_saveg.c`: RP2040 flash save slots stubbed (no slots, writes fail) until the `saves`
    partition is wired.
  - `doom/m_menu.c`, `d_loop.c`: two leftovers that only compile with networking on.
- `esp/`: the platform layer. `i_video.c` is the LCD fork's file with scanvideo, text mode, the
  second core, the interpolator, and the status-bar XIP DMA trick removed, and `fill_scanlines`
  rewritten to compose each 320-pixel 16-bit scanline (palette + overlays exactly as upstream),
  downsample it 8:7 to 280, and stream 16-row strips through the display driver's DMA buffers.
  `i_system.c` (static 64 KB zone, `I_Error` = print + abort, `I_Quit` = `esp_restart`),
  `i_timer.c`, `i_input.c`, `i_sound_glue.c` (pico-extras audio buffer pool API implemented as a
  time-paced sink so the untouched RP2040 mixer runs; OPL music module stubbed), `doom_main.c`.
- `shim/`: `pico.h`, `pico/sem.h` (single-thread counting semaphores that never block),
  `pico/divider.h`, `pico/audio_i2s.h`, `config.h`, and empty `hardware/*.h`.

Compile flags are upstream's `small_doom_common` + `doom_tiny` + `render_newhope` sets with
`USE_EMU8950_OPL=0`, `NO_USE_NET=1`, `USE_PICO_NET=0`, `SUPPORT_TEXT=0`, `NO_USE_ENDDOOM=1`, and
without `WHD_SUPER_TINY`/`DEMO1_ONLY` (all three demos run). Toolchain warnings are silenced for
the engine sources (`-w`), the code is not ours to clean.

## Single-core frame path (as built)

`D_Display` -> `pd_begin_frame` -> BSP traversal into column lists -> `pd_end_frame`: visplane
markers, `draw_visplanes` (was core 1), `draw_regular_columns(0)` (drains the whole queue),
fuzz, then `I_DisplayFrame()`: `new_frame_stuff()` flips the display state and rebuilds the
overlay lists and palette, `fill_scanlines()` composes 200 scanlines into 13 DMA strips. The
strip submit waits for the previous strip, so display time is essentially the SPI wire time.

## Short-pointer window

Upstream's 16-bit, 4-byte-granular short pointers reach 256 KB from `SHORTPTR_BASE`. They point
at zone blocks and also at two statics (`thinkercap`, `players[]`), so `.bss` must end below
0x40840000. Measured `_bss_end`:

| Build | `_bss_end` | Spare |
|---|---|---|
| Phase 3 first light (96 KB zone, 47 KB list buffer static) | 0x4083D170 | 11,920 B |
| + NimBLE linked (phase 4 build) | 0x40847B88 | overrun by 31 KB |
| + list buffer on heap, zone 64 KB | 0x40832E08 | 53,752 B |

`main.c` checks `_bss_end` at boot and aborts with a message rather than let a short pointer
silently truncate.

## Measured, first run (`phase3-run1.log`)

Heap before Doom (this build had the 96 KB zone and 47 KB list buffer in .bss, 521 KB image):

| Point | Free heap | Largest block |
|---|---|---|
| Boot | 239,892 | 212,992 |
| After display | 220,408 | 192,512 |
| After WHD mmap | 219,776 | 192,512 |
| In demo (frames 256..2048) | 101,376 | 75,776 |

Zone: 64-74 KB free of 96 KB during the E1M1 demo, so 22-26 KB in use at peak.
Main task stack: 30,876 of 32,768 bytes never touched, so Doom uses under 2 KB of stack here.

Frame times (`type` is the video mode: 4 = full-screen single buffer, 5 = wipe, 3 = 3D view):

| Mode | Frames | Mean | Min | Max | Display share | FPS |
|---|---|---|---|---|---|---|
| Title screen (4) | 167 | 28.4 ms | 12.2 | 33.8 | 11.7 ms | 35.2 (tic-bound) |
| Wipe (5) | 73 | 13.5 ms | 12.7 | 59.2 | 12.0 ms | 74.0 |
| Demo, 3D view (3) | 885 | 37.5 ms | 12.5 | 51.2 | 11.7 ms | **26.7** |

The 11.7 ms display share is the wire time of a 280x200 RGB565 frame at 80 MHz SPI; the
conversion runs in the shadow of the DMA. The title screen sits at 35 FPS because Doom's loop
is gated by the 35 Hz tic clock, which also confirms `I_GetTime` is right (the demo runs at
correct speed; a wrong tic rate desyncs the demo visibly).

## Ten-minute soak (`phase3-soak-10min.log`)

- Captured 629 s of uptime, 16,895 frames, 17,062 log lines. Reset markers in the log: one,
  the deliberate reset that started the capture. No watchdog, panic, abort, or `I_Error`.
- Heap free stayed at 101,376 bytes (largest block 75,776) from frame 256 to frame 16,640.
  Zone free never dropped below 68,860 of 98,304 bytes: **29.4 KB peak zone use** across all
  three shareware demos. Main task stack high-water unchanged at 30,876 free.
- All three demos cycled with the title screen and wipes between them (mode transitions at
  frames 242, 3870, 4214, 6995, 7340, 8809, 9126, 12749, 13096, 15855, 16200).

| Mode | Frames | Mean | Min | Max | Display share | FPS |
|---|---|---|---|---|---|---|
| Title screen (4) | 1,131 | 28.4 ms | 12.2 | 34.0 | 11.6 ms | 35.2 (tic-bound) |
| Wipe (5) | 807 | 13.7 ms | 12.8 | 64.1 | 12.1 ms | 73.2 |
| Demos, 3D view (3) | 14,956 | 39.8 ms | 12.5 | 77.8 | 11.7 ms | **25.1** |

1,091 of the 14,956 3D frames (7.3%) took longer than 50 ms; the worst was 77.8 ms. The
display's 11.7 ms is fixed cost per frame (SPI wire time). Everything else is engine.

## Gate

- Recognizable Doom on the panel: confirmed by Jesse looking at the medal ("looks like it's
  running well").
- Frame time logged per frame: yes, every frame prints total and display microseconds.
- No watchdog resets over ten minutes of demo playback: yes, 629 s, zero resets.

Phase 3 gate met.
