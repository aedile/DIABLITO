# Phase 1: Orientation

Date: 2026-09-14. Gate: notes written, hardware map complete, reference code identified, host
`chocolate-doom` builds, `whd_gen` produces a WHD from the shareware `doom1.wad`, WHD fits the
planned partition with headroom.

## What was set up

- `third_party/rp2040-doom` — `kilograham/rp2040-doom` at `29a453c` (2024-08-11, "rp2350
  support and a minor fix"). Shallow clone.
- `third_party/rp2040-doom-LCD` — `rsheldiii/rp2040-doom-LCD` at `14a2b48` (2025-07-26).
  Shallow clone.
- `third_party/doom1.wad` — shareware 1.9, 4,196,020 bytes,
  md5 `f0cefca49926d00903cf57551d901abe`, sha1 `5b2e249b9c5133ec987b3ea77596381dc0d6bc1d`
  (matches the published checksums). The ibiblio, doomworld, and archive.org URLs all returned
  HTML or XML error pages; the copy came from a GitHub mirror and was verified by checksum.
- `NOTES/writeup/` — Graham Sanderson's write-up as plain text (intro, rendering, flash,
  speed_and_ram, sound, dev_overview). Read in full.
- Host toolchain: Apple clang, CMake 4.4.2, SDL2 via `sdl2-compat`, plus `brew install
  sdl2_mixer sdl2_net` (installed this session).

## Host build

```sh
cd third_party/rp2040-doom && mkdir build-host && cd build-host
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wno-missing-template-arg-list-after-template-kw" ..
make -j8 chocolate-doom whd_gen
```

- `chocolate-doom` built clean on the first try.
- `whd_gen` needed the extra `CMAKE_CXX_FLAGS`: current clang promotes
  `-Wmissing-template-arg-list-after-template-kw` to an error in `huffman.h`, `huff_sink.h`, and
  `whd_gen.cpp:760`. No source was modified.
- Sanity run of the host executable against the WAD:

```
./src/chocolate-doom -iwad ../../doom1.wad -nosound -nomusic -nosfx -timedemo demo1 -window
timed 5026 gametics in 557 realtics (315.816864 fps)
```

## WHD generation (measured)

`whd_gen` reports the uncompressed input as 4,175,556 bytes across 1264 lumps. Both output
formats were generated in about 2 seconds each:

| File | Bytes | MiB | Command |
|---|---|---|---|
| `build-artifacts/doom1.whx` | 1,800,312 | 1.72 | `whd_gen doom1.wad doom1.whx` |
| `build-artifacts/doom1.whd` | 2,073,676 | 1.98 | `whd_gen doom1.wad doom1.whd -no-super-tiny` |

The generated WHX is 32 bytes smaller than the `doom1.whx` shipped in the repo and differs
from byte 13 onward, which is consistent with a newer `whd_gen` than the committed file. It
does not matter: we regenerate.

Compressed sections that dominate: SFX 523 KB (ADPCM), sidedefs 286 KB, music 239 KB, segs
126 KB.

### Decision: WHD, not WHX

The RP2040 needed the super-tiny WHX to fit 2 MB. We have 16 MB. The super-tiny build
(`doom_tiny`) also forces `DEMO1_ONLY=1`, `NO_USE_FINALE_CAST=1`, `NO_USE_FINALE_BUNNY=1`.
`DEMO1_ONLY` would cripple the attract loop that the medal use case depends on, and the WHD
decoder has fewer limits. Use the `doom_tiny_nost` configuration (`USE_WHD=1`, no
`WHD_SUPER_TINY`) with `doom1.whd`.

## Planned partition table (16 MB flash)

| Name | Type | Offset | Size | Use |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0x6000 | settings, BLE bonds |
| phy_init | data/phy | 0xf000 | 0x1000 | |
| factory | app | 0x10000 | 0x200000 (2 MiB) | IDF + NimBLE + Doom code |
| wad | data/0x40 | 0x210000 | 0x400000 (4 MiB) | `doom1.whd` |
| saves | data/nvs | 0x610000 | 0x20000 (128 KiB) | compressed savegames |

**Gate check: WHD is 2,073,676 bytes in a 4,194,304-byte partition, 49.4% used, 2.02 MiB of
headroom.** Total flash used 6.2 MB of 16 MB. The `wad` partition is written with
`esptool_py_flash_to_partition` the way NESTOR writes `roms`.

## Codebase reading: what the port has to replace

Pico-sdk surface actually used by the engine (grep of `src/`, excluding `whd_gen`):

| pico-sdk / pico-extras API | Where | ESP-IDF replacement |
|---|---|---|
| `pico/scanvideo` + PIO `video_doom.pio`, DMA to PIO | `src/pico/i_video.c` | delete; the LCD fork already did this and left a per-scanline `I_handleScanline(uint16_t *line, int)` hook |
| `pico/multicore`, `pico/sem`, `pico/mutex`, `pico/sync` (core1 runs `pd_core1_loop` + `fill_scanlines`) | `pd_render.cpp` (26 uses), `i_video.c` (10), `i_system.c` (3), `p_saveg.c` (2) | collapse to one task: render, then fill scanlines, in sequence; semaphores become no-ops or plain flags |
| `hardware/interp` (RP2040 interpolator) | `pd_render.cpp` (23 lines), `i_video.c` (18) | already behind `USE_INTERP`; the LCD fork sets it to 0 and the C fallbacks exist |
| ARM asm: `src/pico/blit.S`, inline asm in `m_fixed.h`, `doomtype.h`, `pd_render.cpp`, `i_video.c` | | C fallbacks; `FixedMul` is a native `mulh` on RV32IM, no asm needed |
| `pico/audio_i2s` (pico-extras) | `src/pico/i_picosound.c` | IDF I2S std driver + ES8311 from the reference projects, DAC-paced pull |
| `pico/time`, `sleep_ms`, `time_us_64` | `src/pico/i_timer.c` (61 lines) | `esp_timer_get_time`, `vTaskDelay` |
| `hardware/flash` + boot2 copyout (`picoflash.c`) | savegames to flash | `esp_partition_write` / NVS blobs |
| XIP address `TINY_WAD_ADDR` | `w_file_static.c`, `USE_MEMORY_WAD` | `esp_partition_mmap` pointer |
| `pico/divider` hardware divider | | native `div` |
| `__scratch_x`, `__not_in_flash_func` placement | 8 in `i_video.c`, `tiny_huff.c`, `image_decoder.c`, linker script `memmap_doom.ld` | `IRAM_ATTR` for the hot loops; the C6 cache is 32 KB vs the RP2040's 16 KB |
| TinyUSB keyboard host, `piconet` I2C networking | `i_input.c`, `piconet.c` | delete; input comes from `ble_pad` + two GPIOs |
| `USE_ZONE_FOR_MALLOC`, `__end__`/`SRAM4_BASE` zone sizing | `i_system.c:140` | static zone array inside the 256 KB shortptr window |

Emulated OPL2 music (`USE_EMU8950_OPL`, `EMU8950_ASM=1`) runs on core 1 in the RP2040 build. It
stays behind a compile flag and off for this port until the single core is measured with
everything else running.

### The two upstream trees

Use **`kilograham/rp2040-doom` upstream** as the base, not the LCD fork. Upstream is a year
newer, parameterizes `SHORTPTR_BASE`, and the LCD fork's only substantive deltas are in
`src/pico/i_video.c` (1519 -> 1182 lines: scanvideo removed, `fill_scanlines` rewritten to loop
200 scanlines through a `uint16_t buffer[SCREENWIDTH]` and call `I_handleScanline`),
`src/pico/pico-screens/` (per-panel blit + a rough nearest-neighbour downsampler whose
area-average variant is marked broken by its author), and `src/CMakeLists.txt` target plumbing.
Those i_video.c changes are what we port over onto upstream.

Note that even the LCD fork still runs `fill_scanlines` on core 1 and does a blocking SPI write
per scanline. Neither is acceptable here; that path becomes the DMA strip push.

## Display fit decision: 320x200 -> 280x200, 1:1 vertical

Panel in landscape is 280x240. Options considered:

1. **Crop 320 -> 280** (20 px off each side), 1:1. Cheapest possible inner loop but cuts the
   status bar: the ammo count is right-aligned at x=44 (digits start at x=2) and the per-weapon
   ammo table ends at x=314. Rejected.
2. **Horizontal 7:8 nearest neighbour** (emit 7 of every 8 source pixels), vertical 1:1, 20 px
   black bands top and bottom. Inner loop stays "one palette lookup per output pixel", just
   unrolled 8-in/7-out with no index table. Aspect comes out 1.40 vs the original 1.33, a 5%
   stretch nobody will see on a 1.69" panel. **Chosen for phase 3.**
3. Same as 2 plus vertical 200 -> 210 or 240 by duplicating rows. Row duplication is free at the
   strip-push level (push a source row twice), so this is a later cosmetic knob, not a renderer
   change.
4. Render at 280 wide natively (`SCREENWIDTH` change). Touches status bar, menu, and intermission
   coordinates throughout Chocolate Doom and every vpatch offset. Not for first light.

The downsample happens on the composed 16-bit scanline after overlays (status bar, menus) are
drawn, so overlay code stays at 320 wide untouched.

## Frame path plan for one core

Per game frame: `D_RunFrame` (tics + BSP traversal into column lists) -> `pd_render` column
lists to the 8-bit framebuffer (was split across two cores; now sequential) -> `fill_scanlines`
converts 200 scanlines with palette + overlays into 16-bit, downsamples to 280, and writes into
one of two 16-row DMA strips -> `spi_device_queue_trans`. The CPU converts strip N+1 while
strip N is on the wire. Expect the RP2040's 30-35 FPS to become well under 20 FPS before
optimization: we have one 160 MHz core against two at 270 MHz, though with a hardware multiplier,
a hardware divider, and a 32 KB cache the per-core gap is smaller than the clock ratio suggests.

## Open questions carried to phase 2

- Real `esp_partition_mmap` limit on the C6 with a 2 MB app plus a 2 MB WHD (NESTOR hit a
  limit at 6 MB). Measured read bandwidth from the mapped region.
- Free heap and largest block at boot and after BLE, on this firmware, not NESTOR's.
- Whether the IDF main task can be given a 16 KB stack and still leave the 256 KB zone window.
