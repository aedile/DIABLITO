# Memory budget

All numbers are from the device over serial unless marked "planned". Logs live in
`NOTES/logs/`.

## What the reference port needs (RP2040 Doom, from the write-up and source)

RP2040 has 264 KB total (256 KB main + 2 x 4 KB scratch) and RP2040 Doom uses essentially all
of it. Rough breakdown from "Making It Run Fast And Fit in RAM":

| Item | Bytes | Where |
|---|---|---|
| Two 320x168 8-bit framebuffers | 107,520 | `frame_buffer[2][SCREENWIDTH*MAIN_VIEWHEIGHT]` |
| Column display lists + one flat | 47,296 | `list_buffer[RENDER_COL_MAX * sizeof(pd_column) + 64*64]`, `RENDER_COL_MAX 3600` |
| visplane bit buffer | 6,720 | `pd_render.cpp` `visplane_bit` |
| vpatch overlay lists | < 3,072 | `vpatchlists_t` |
| Zone heap + malloc (combined) | ~58 KB, zone peaks ~45 KB per level | `USE_ZONE_FOR_MALLOC` |
| Decoder scratch, 8 sound channels (~2.2 KB), palettes, stacks | remainder | |

Constraints that carry over: 16-bit short pointers with 4-byte granularity reach 256 KB from
`SHORTPTR_BASE`, and they point at `thinkercap` and `players[]` (statics) as well as zone
blocks. On the C6 `SHORTPTR_BASE` is 0x40800000 (start of SRAM), so everything short-pointed
must live in `.bss` and `.bss` must end below 0x40840000. The framebuffers and the column list
buffer are therefore on the heap, the zone is a static array.

## ESP32-C6 measured

### Phase 2 skeleton (IDF + display driver, NimBLE configured but not linked)

| Point | Free heap | Largest block |
|---|---|---|
| Boot | 445,956 | 417,792 |
| After display init | 426,472 | 401,408 |
| After WHD mmap | 425,840 | 393,216 |

### Phase 3 first light (Doom linked, 96 KB zone + 47 KB list buffer static, no BLE)

| Point | Free heap | Largest block |
|---|---|---|
| Boot | 239,892 | 212,992 |
| After display | 220,408 | 192,512 |
| In demo, steady over 10 min | 101,376 | 75,776 |

Doom's heap use once running: ~119 KB (framebuffers 107,520 + vpatchlists + misc).
Zone in use during the E1M1 demo: 22-29 KB of 96 KB (`Z_FreeMemory` 67-74 KB).
Main task stack: under 2 KB used of 32 KB (high-water 30,876 free).

### Phase 4 build changes (NimBLE linked)

Linking NimBLE adds about 43 KB of `.bss`, which pushed `_bss_end` to 0x40847B88, past the
short-pointer window. Fixes applied: list buffer moved to the heap (47 KB), zone cut from 96 KB
to 64 KB (2.3x the measured demo peak, above the RP2040's quoted 45 KB worst case), main task
stack cut from 32 KB to 16 KB (8x the measured use), phase-2 test pattern buffer removed.
`_bss_end` is now 0x40832E08 with 53,752 bytes of window to spare.

### Phase 4 measured (BLE running, `phase4-run2.log`)

| Point | Free heap | Largest block |
|---|---|---|
| Boot | 293,380 | 262,144 |
| After display + WHD mmap | 273,264 | 245,760 |
| After BLE init | 234,392 | 208,896 |
| In E1M1 with BLE scanning | 67,708 | 41,984 |

BLE costs ~39 KB heap + ~43 KB `.bss`. Zone peak 25 KB of 64 KB. Main stack 14,540 of 16,384
free. **67 KB of heap headroom** is what phase 5 (audio: ~8 KB DMA ring + 2 x 4 KB mix
buffers) and phase 7 have to live in.

## Layout on the C6 (current)

| Item | Bytes | Where |
|---|---|---|
| Two 320x168 8-bit framebuffers | 107,520 | heap, one block, `I_InitGraphics` |
| Column lists + flat cache | 47,296 | heap, `pd_init` |
| Zone | 65,536 | `.bss` (short-pointer window) |
| Display DMA strips | 2 x 8,960 | `MALLOC_CAP_DMA` heap |
| Sound channel state | 2,176 | `.bss` |
| Main task stack | 16,384 | Doom runs on the IDF main task |
| NimBLE + HID host + pad task stacks | 4 x 4 KB + pools | heap |

Flash: app image 1,031,808 bytes (phase 4 build) of a 2 MiB partition; WHD 2,073,676 bytes of
a 4 MiB partition.
