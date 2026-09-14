# DIABLITO

Shareware Doom on a Waveshare ESP32-C6-LCD-1.69, worn as a Fiesta San Antonio medal. A port of
[kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom) (Chocolate Doom derivative)
from the RP2040 to ESP-IDF on a single RISC-V core with 512 KB of SRAM and no PSRAM.

Status and every measurement live in [`NOTES/`](NOTES/): hardware map, memory budget, one file
per phase with the numbers from the device, and the list of what was cut.

## Build and flash

ESP-IDF v5.3.4 via Docker, flashing from the host:

```sh
./build.sh                                  # idf.py build in espressif/idf:v5.3.4
./flash.sh                                  # bootloader, partitions, app, doom1.whd
./tools/monitor.py 60 log.txt               # reset and capture the console
./tools/monitor.py 60 log.txt --script "20:q,23:j"   # also press bench-pad keys (see components/doom/esp/i_input.c)
```

`build-artifacts/doom1.whd` is generated from the shareware `third_party/doom1.wad` with
upstream's `whd_gen` (`whd_gen doom1.wad doom1.whd -no-super-tiny`); see
`NOTES/phase-1-orientation.md` for the host build.

## Layout

- `components/doom/` — the engine: `src/` is upstream `rp2040-doom/src` with small patches,
  `esp/` the ESP-IDF platform layer, `shim/` pico-sdk headers mapped to IDF.
- `components/display/` — ST7789 over SPI DMA (from NESTOR/PELLETINO).
- `components/ble_pad/`, `components/esp_hid/` — BLE HID gamepad (from NESTOR).
- `main/` — boot, WHD partition mmap, BLE start, medal buttons.
- `third_party/` — upstream repos as submodules, plus the shareware WAD.

## License

Engine code derived from Chocolate Doom / RP2040 Doom is GPLv2 (see `third_party/rp2040-doom/COPYING.md`).
New RP2040 Doom code is BSD-3. Code written for this port is GPLv2. Only the freely
redistributable shareware `doom1.wad` is included.
