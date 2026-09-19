# Music (OPL2)

Date: 2026-09-19. Logs: `NOTES/logs/music-run1.log` (full rate), `music-run2-halfrate.log`,
`music-run3.log` (final).

## What was built

Upstream's music path, unchanged in structure: `i_oplmusic.c` plays the WHD's compressed MUS
(MUSX) data into `emu8950`, Graham Sanderson's cut-down OPL2 emulator, through `opl_pico.c`,
whose `OPL_Pico_Mix_callback` is the mixer's music generator. Sound effects are mixed on top in
the same buffer. Portable pieces needed:

- `EMU8950_ASM=0` and a local `OPL_RP2040_HW 0` switch in `slot_render.cpp/.h` (the file used
  `PICO_ON_DEVICE` to mean "RP2040 interpolators and ARM asm").
- `shim/pico/util/pheap.h`: the callback queue's pairing heap as a ten-entry sorted list.
- The mixer fills up to four 1,024-frame buffers per update (a frame here outlasts one buffer),
  and the glue carries over any tail the I2S driver did not accept instead of dropping it.

## Full rate was too expensive

The emulator is only pitch-correct at the chip's native rate (clock / 72 = 49,716 Hz), which is
what upstream runs. In portable C on this core that cost about 15 ms per frame:

| Build | 3D frame mean | FPS | Worst | Heap free |
|---|---|---|---|---|
| No music (phase 6) | 29-30 ms | 33-34 | 53 ms | 46.6 KB |
| OPL at 49,716 Hz | 44.4 ms | 22.5 | 79.9 ms | 29.3 KB |
| **OPL at 24,858 Hz, register-corrected** | **33.9 ms** | **29.5** | 64.5 ms | 37.7 KB |

## The half-rate trick

Clock the chip at half its native sample rate and everything time-based runs at half speed. OPL2
allows undoing that exactly where the driver writes registers (`opl_pico.c`, `OPL_HALF_RATE`):

- `0xB0-0xB8`: block number +1 = one octave up = 2x frequency.
- `0x60-0x75` and `0x80-0x95`: attack, decay and release rates +1 = 2x envelope speed.

Left uncorrected: LFO vibrato/tremolo run at half speed, and notes already at block 7 stay an
octave low. Both are rare in Doom's music and subtle on this speaker. The mixer and I2S run at the
same 24,858 Hz, so sound-effect mixing got cheaper too and the DMA ring stayed at 8 KB (82 ms).

## Measured (final)

4,190 3D frames across the attract demos, a menu-started new game and E1M1 play: mean 33.9 ms
(29.5 FPS), worst 64.5 ms, 1.3% of frames over 50 ms. Heap free 37,664, steady. No short writes.
Underruns happen in exactly two places, about 80 descriptors each: during boot before the first
mix, and while a level loads (where the song changes anyway). None during play.
