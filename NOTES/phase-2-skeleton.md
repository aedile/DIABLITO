# Phase 2: Skeleton

Date: 2026-09-14. Firmware: `main/main.c` + `components/display/` + lifted `main/medal.c`.
Full log: `NOTES/logs/phase2-skeleton-boot.log`.

## Build and flash

- `./build.sh` (Docker `espressif/idf:v5.3.4`) -> `diablito.bin` **193,024 bytes**, 91% of the
  2 MiB factory partition free. That is IDF + drivers + NimBLE configured but not started.
- `./flash.sh` writes bootloader, partition table, app, and `doom1.whd` at 0x210000 in one
  pass (`esptool_py_flash_to_partition(flash "wad" ...)`). WHD write: 2,073,676 bytes in 8.7 s,
  hash verified.
- `tools/monitor.py [secs] [logfile]` resets the board over USB-Serial-JTAG and captures the
  console. It runs under esptool's own Python because that is where pyserial lives.

## Measured on the device

| Point | Free heap | Largest block | DMA-capable |
|---|---|---|---|
| Boot, before drivers | 445,956 | 417,792 | 431,356 |
| After display init (2 x 8,960 B DMA strips + SPI driver) | 426,472 | 401,408 | 411,872 |
| After WHD mmap | 425,840 | 393,216 | 411,240 |
| Idle steady state | 425,536 | 393,216 | 410,936 |

IDF reports 430 KiB + 11 KiB of SRAM heap plus 15 KiB RTC RAM. So the OS, bootloader-side
allocations, and our 193 KB image's .data/.bss together cost about 66 KB of the 512 KB. Main
task stack high-water: 6,664 of 8,192 bytes free after all of the above.

Display: one full 280x240 frame through the indexed strip path took **14.66 ms**, of which
3.53 ms was blocked waiting on DMA. The wire time for 134,400 bytes at 80 MHz is 13.4 ms, so
the test pattern is SPI-bound as expected; the conversion loop is hidden behind the transfer.
A 280x200 Doom frame will need about 11.2 ms of wire time per frame, overlappable with the
next frame's render.

WHD partition: `wad` at 0x210000, 4,194,304 bytes. Header read via `esp_partition_read`:
`IWHD`, 1264 lumps, info table at 36, file size 2,073,676. `esp_partition_mmap(0, 2073676,
DATA)` succeeded and returned 0x42030000; the magic and `DOOM1.WAD` name read back correctly
through the mapping. (The app's own flash mapping ends below 0x42030000, so the 2 MB WHD sits
right after it in the 16 MB window. NESTOR's 6 MB failure is not reproduced at this size.)

Flash read speed through the cache (160 MHz CPU, QIO 80 MHz flash, 32 KB cache):

| Pattern | Result |
|---|---|
| Sequential sum over the whole 2,073,676-byte WHD | 86.6 ms = **23.9 MB/s** |
| Random 4-byte reads spread over 2 MB (200,000 reads) | **1,296 ns each** (about 207 CPU cycles) |
| Random 4-byte reads spread over 64 KB of flash | 675 ns each |
| Same random pattern over 64 KB in SRAM | 69 ns each (includes the LCG and loop) |

The 1.3 µs cache-miss cost is the number to design the renderer around: Doom's level data
and texture chasing is scattered. RP2040 Doom paid a similar per-miss price on its 16 KB XIP
cache and still ran, but it is the first place to look when frame time disappoints. Sequential
decode of a compressed column or flat is cheap.

Battery ADC on USB: 4,203 mV reported, matching a charged cell being topped up.

## Gate

- WHD header readable from the mapped partition: **yes**, logged above.
- Free heap and largest free block logged: **yes**, table above.
- Test pattern on screen: the frame was clocked out at the expected wire rate with no SPI
  errors, using MADCTL 0x60 and the column offset that NESTOR ships with on this same panel.
  **Visual confirmation of orientation and colour bars still needs a human looking at the
  medal**; nothing in the serial log can prove pixels. Expected picture, landscape: eight
  colour bars (white, yellow, cyan, green, magenta, red, blue, black) across the top third, a
  black-to-white ramp in the middle third, an 8 px checkerboard in the bottom third.
