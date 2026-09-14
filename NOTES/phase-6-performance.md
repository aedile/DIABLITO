# Phase 6: Performance

Date: 2026-09-14. Target from the brief: stable 20 FPS at the resolution settled on
(280x200 on the panel, 320x200 internal). Logs: `NOTES/logs/phase6-*.log`.

## Starting point (end of phase 5)

3D view frames: 39.1 ms mean in E1M1, 37.7 ms in the demos, of which 11.7 ms was the game
task waiting on the SPI while a finished frame clocked out.

## What was tried, in order, with the demo as the yardstick (first 1,200 3D frames of a run)

| Change | Demo mean | Result |
|---|---|---|
| Baseline, phase 5 build | 37.7 ms | |
| **Display on its own task** (real FreeRTOS semaphores behind the pico `sem` shim, so the RP2040's core-0/core-1 handshake works as designed; game renders frame N+1 while frame N streams) | 32.4 ms | kept. Gain is the wire time minus the ~5 ms of conversion CPU that now preempts the game |
| Per-phase timers (`pd_render.cpp`, prints every 128 frames) | | logic+BSP 7-13 ms, visplane marks 1.5, flats 3.5-7, columns 9-14, fuzz <1 |
| **19.5 KB of the draw functions in IRAM** (`draw_patch_columns`, `draw_composite_columns`, `pd_end_frame`, `decode_flat_to_slot`, `th_read_simple_decoder`) | 33.5 -> 31.8 ms | reverted: ~5% for 20 KB of heap (47 -> 27 KB). Instruction cache misses are not the bottleneck |
| **Fused convert + downsample** for 3D-view rows with no overlay (four source pixels per load, straight into the DMA strip) | 33.5 -> 31.5 ms | kept |
| 2 kHz `mepc` sampling profiler (`esp/prof.c`, `tools/symbolize.sh`) | | `draw_patch_columns` 37% of all samples, `flush_visplanes` 9%, strip conversion 8%, `pd_end_frame` 7%, status bar overlays 3%, `R_RenderSegLoop` 2% |
| **`NDEBUG`** (the RP2040 device build was MinSizeRel; here every `assert()` in the Huffman decoders and the column loops was live) | 31.5 -> 29.4 ms | kept. Columns phase 9-14 -> 7-8.6 ms |
| Four-way unroll of the column and span pixel loops | 29.4 -> 29.3 ms | reverted: the loops are bound by strided framebuffer stores and texture reads |

Not tried, and why: resolution or detail scaling would need the fixed-`SCREENWIDTH` tables and
status bar coordinates reworked, and the target was met without it; RISC-V assembly for the two
pixel loops is unlikely to beat what the profiler shows (memory-bound loops); OPL music remains
off.

## Final numbers (this build, `phase6-final-game.log`, `phase6-final-demo.log`)

E1M1, scripted 70 s of play (movement, turning, firing, doors), 2,222 3D frames:

| Mean | p50 | p90 | p99 | Max | Frames over 40 ms |
|---|---|---|---|---|---|
| 29.0 ms (**34.5 FPS**) | 28.8 ms | 30.2 ms (33 FPS) | 35.2 ms (28 FPS) | 36.7 ms | 0 |

Attract loop, five-minute soak, 9,569 3D frames across all three demos:

| Mean | p50 | p90 | p99 | Max | Frames over 40 ms |
|---|---|---|---|---|---|
| 30.1 ms (**33.2 FPS**) | 29.3 ms | 34.6 ms (29 FPS) | 41.3 ms (24 FPS) | 53.5 ms (19 FPS) | 1.7% |

Heap free steady at 46,600 (largest block 21 KB) with BLE and sound running; zone 39-53 KB
free of 64 KB; no resets. Display share per frame unchanged at 11.6 ms but now off the game
task's critical path.

Honest reading against the target: the 3D view runs at 33-35 FPS on average and holds above
28 FPS for 99% of frames in both gameplay and the demos. The single worst frame seen in 9,569
was 53 ms (19 FPS), once. Stable 20 FPS is met with margin; it is not a locked 35.

Gate met.
