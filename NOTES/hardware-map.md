# DIABLITO hardware map

Board: Waveshare ESP32-C6-LCD-1.69. Sources: PELLETINO (`../PELLETINO`, a symlink to
`../MINIMAME/games/PELLETINO`), MINIMAME (`../MINIMAME`), NESTOR (`../NESTOR`), and the
Waveshare Arduino examples in `../ESP32-C6-1.69inch-LCD/examples`. All three projects agree on
every pin below. Read on 2026-09-14.

## Silicon and flash (measured)

| Item | Value | Source |
|---|---|---|
| SoC | ESP32-C6, single RISC-V core, 160 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160`) | all sdkconfigs |
| SRAM | 512 KB HP SRAM at 0x40800000, no PSRAM | datasheet, no PSRAM options in any sdkconfig |
| Flash | **16 MB**, Winbond `ef 4018` (W25Q128) | `esptool flash_id` on the connected board, `/dev/cu.usbmodem2101` |
| Flash mode | QIO selected, IDF resolves to `dio` string + STR sample mode, 80 MHz | NESTOR/MINIMAME sdkconfig |
| Flash MMU | 256 entries x 64 KB pages = 16 MB virtual window shared by IROM and DROM (`SOC_MMU_ENTRY_NUM 256`, `SOC_MMU_PAGE_SIZE 0x10000`) | IDF v5.3.4 `soc/esp32c6/ext_mem_defs.h` |
| Console | USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in NESTOR) | NESTOR sdkconfig.defaults |

PELLETINO's `partitions.csv` says 4 MB flash; that is stale. MINIMAME and NESTOR both use 16 MB and
the board confirms it.

NESTOR notes (`main/main.c:73`) that mapping its whole 6 MB `roms` partition with
`esp_partition_mmap` failed, so it maps only the used span. The MMU window is 16 MB on paper;
whatever the real limit is, it has to be measured in phase 2 with the actual WHD size.

## GPIO

| Function | GPIO | Notes |
|---|---|---|
| LCD SCLK | 1 | SPI2_HOST |
| LCD MOSI | 2 | MISO not connected (-1) |
| LCD DC | 3 | driven in `spi_device_interface_config_t.pre_cb` from `t->user` |
| LCD RST | 4 | manual reset, low 100 ms then high 100 ms, before bus init |
| LCD CS | 5 | hardware CS |
| LCD backlight | 6 | LEDC low-speed, timer 0, channel 0, 5 kHz, 8-bit; 153 active, 76 idle |
| I2C SCL | 7 | I2C_NUM_0, 100 kHz, shared ES8311 (0x18) + QMI8658 IMU (0x6B) |
| I2C SDA | 8 | |
| BOOT button | 9 | active low, internal pull-up |
| PWR button | 18 | active low, internal pull-up; the third button is RST (CHIP_PU), not software visible |
| BAT_EN | 15 | **must be driven high early or the board cuts its own power**; drive low to power off |
| Battery ADC | 0 | ADC1 channel 0, `ADC_ATTEN_DB_12`, curve-fit calibration, divider x3 |
| I2S MCLK | 19 | |
| I2S BCLK | 20 | |
| I2S DIN | 21 | codec ADC, unused |
| I2S LRCK | 22 | |
| I2S DOUT | 23 | |
| Amp enable | none | `GPIO_NUM_NC`; power-down is via codec registers |

## Display: ST7789 240x280

Driver is raw `spi_master`, not `esp_lcd`, in every project. Config that is known to work:

```c
spi_bus_config_t buscfg = { .mosi_io_num = 2, .miso_io_num = -1, .sclk_io_num = 1,
                            .quadwp_io_num = -1, .quadhd_io_num = -1,
                            .max_transfer_sz = DMA_BUFFER_SIZE };
spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
spi_device_interface_config_t devcfg = { .clock_speed_hz = 80000000, .mode = 0,
                                         .spics_io_num = 5, .queue_size = 7,
                                         .pre_cb = lcd_spi_pre_transfer_callback };
```

- 80 MHz SPI clock works on this panel in all three projects.
- COLMOD 0x55 (RGB565), INVON on. Pixels go out big-endian, so palettes are pre-swapped once and
  the DMA copy is a plain memcpy.
- Panel RAM is 240x320; the visible 280 rows start at offset 20 on the long axis. Portrait
  MADCTL 0x00 puts the +20 on RASET. Landscape MADCTL 0x60 (MX|MV) puts it on CASET; NESTOR has
  this path, PELLETINO and MINIMAME do not.
- No full framebuffer anywhere. Two DMA strip buffers (`heap_caps_malloc(..., MALLOC_CAP_DMA)`)
  of 16 rows each, queued with `spi_device_queue_trans` while the other strip is being converted.
- NESTOR's `display_push_strip` (`components/display/src/display.cpp:249`) is IRAM, converts an
  8-bit indexed row to pre-swapped RGB565 two pixels per 32-bit store, and is exactly the shape
  Doom needs. Wire time for one 280x240 RGB565 frame at 80 MHz is 13.4 ms; for 280x200 it is
  11.2 ms.
- Flash-mapped memory cannot be a DMA source (MINIMAME ARCHITECTURE.md).
- Dynamic frequency scaling breaks SPI DMA on this chip. PELLETINO enables `CONFIG_PM_ENABLE` but
  never calls `esp_pm_configure`; NESTOR sets `CONFIG_PM_ENABLE=n`. Do the latter.

Verdict on PELLETINO vs MINIMAME: same file. MINIMAME's launcher copy adds `send_chunked` (the
older copies silently truncate writes larger than one DMA buffer, in 20 of 26 games), and the
PLUMBER-family copy adds zero-copy `display_acquire_buffer`/`display_submit_buffer`. NESTOR's copy
is the newest and adds runtime landscape plus the indexed strip push. **Lift NESTOR's
`components/display/`** and drop the game-width assumptions.

Frame budget handling differs: PELLETINO renders inline and sleeps the remainder with
`vTaskDelay`; MINIMAME games use an owed-time accumulator capped at 3 frames, draw only the last
frame of a catch-up burst, and hand frames to a priority-6 render task through two queues;
NESTOR has no delay at all and lets the blocking I2S write pace the loop. None of these apply
directly: Doom's own loop (`D_RunFrame` / `TryRunTics`) already paces game tics at 35 Hz and
renders as fast as it can.

## Audio: ES8311 codec over I2S

- I2S_NUM_0, std Philips, 16-bit mono, MCLK wired (256 x fs). Reference rate 20050 Hz (Doom SFX
  are 11025 Hz; the RP2040 mixer resamples with a 16:16 step, so 22050 Hz mono is a fine target).
- I2C control at 100 kHz. The register bring-up sequence lives in
  `PELLETINO/components/audio_hal/src/audio_hal.cpp:86-119` (magic numbers, duplicated again at
  307-336). NESTOR's `components/audio_hal/audio_hal.c` is the C version.
- Two feeding models exist. PELLETINO/MINIMAME: DAC-paced pull, an IRAM `on_sent` ISR counts bytes
  consumed, `audio_update()` renders exactly the shortfall against a 3-frame target with a
  non-blocking `i2s_channel_write(..., 0)`. NESTOR: `audio_submit()` blocks and is the frame clock.
  Doom wants the first model.
- Mute powers the codec down and deletes the I2S channel; unmute must re-power (a known one-way
  bug in older copies).

## Buttons and power

- Both buttons polled once per frame, active low, no debounce filter; NESTOR applies a 30 ms minimum
  press, the others rely on duration gestures (short press < 400 ms, long holds for power-off,
  mute, exit). NESTOR arms a button only after a release so the power-on hold does not fire.
- Hold BAT_EN (15) high in the first lines of `app_main`.
- Battery cutoff used by MINIMAME: 3250 mV after three strikes, sampled every 5 s.

## BLE gamepad (NESTOR only)

Stack: NimBLE central + observer with IDF's `esp_hid` host vendored and patched. Reuse needs
four pieces together:

1. `NESTOR/components/ble_pad/` (385 lines). Public API is a polled `uint32_t ble_pad_buttons()`
   bitmask (`PAD_UP/DOWN/LEFT/RIGHT/A/B/START/SELECT/MENU`), plus `ble_pad_init`,
   `ble_pad_scan_any`, `ble_pad_scan_rate`, `ble_pad_state`, `ble_pad_name`, `ble_pad_forget`.
   Generic HID report-descriptor parser; only `map_buttons()` (`ble_pad.c:102-117`, Xbox
   numbering) is controller specific.
2. `NESTOR/components/esp_hid/` — IDF v5.3.4's copy with five patches in `src/nimble_hidh.c`
   (all marked `NESTOR`): protocol_mode memset, `dev->connected = true`, pair before discovery,
   GATT read error wakes the waiter, repeat-pairing retry. Without these Xbox pads connect and
   send nothing.
3. The NimBLE sdkconfig block, including the counter-intuitive
   `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y`, `CONFIG_BT_NIMBLE_GATT_SERVER=y`,
   `CONFIG_BT_NIMBLE_HID_SERVICE=y` (IDF only compiles `nimble_hidh.c` behind the HID service
   menu). `MAX_CONNECTIONS=1`, `MAX_BONDS=3`, `NVS_PERSIST=y`, `HOST_TASK_STACK_SIZE=4096`,
   `MEM_ALLOC_MODE_INTERNAL=y`.
4. `nvs_flash_init()` before `ble_pad_init()`. Bond store is NimBLE's NVS store; the saved
   address blob (namespace `nestor`, key `pad`) is only the scan filter.

Tasks it creates: `pad` (stack 4096, prio 5), NimBLE host task (4096), `esp_hidh_events`
(4096, inherits caller priority), BLE controller task (4096). Sync screen flow is in
`NESTOR/main/main.c:353-409`. Only BLE HID pads work: Xbox Wireless 2016+ tested, Stadia
untested, all Bluetooth Classic pads impossible on the C6.

Heap after BLE up in NESTOR (display + audio + BLE, before loading a cart): **286 KB free,
258 KB largest block** (commit b07aee6). In game: 205-230 KB free. No pre-BLE number recorded.

## sdkconfig options the references rely on

Common to all: `CONFIG_IDF_TARGET="esp32c6"`, `CPU_FREQ_MHZ_160`, `FLASHMODE_QIO`,
`FLASHFREQ_80M`, `FLASHSIZE_16MB` (NESTOR/MINIMAME), custom `partitions.csv`,
`COMPILER_OPTIMIZATION_PERF` (-O2; MINIMAME launcher is -Os), `FREERTOS_HZ=1000`,
`FREERTOS_CHECK_STACKOVERFLOW_CANARY`, `SPI_MASTER_IN_IRAM` (+ ISR in IRAM),
`ESP_TASK_WDT_EN=n` (interrupt WDT stays on at 300 ms), `LOG_DEFAULT_LEVEL_INFO`,
`HEAP_POISONING_DISABLED`. NESTOR raises `ESP_MAIN_TASK_STACK_SIZE=8192` (default 3584).
No PSRAM options exist for the C6.

## Partition tables

- PELLETINO (stale 4 MB): nvs 0x6000 @ 0x9000, phy 0x1000 @ 0xf000, factory 0x3F0000 @ 0x10000.
- NESTOR (16 MB): nvs, phy, factory 0x150000 @ 0x10000, `roms` data/0x40 0x600000 @ 0x160000,
  `saves` nvs 0x80000 @ 0x760000. Data partitions are flashed with
  `esptool_py_flash_to_partition(flash "roms" ...)` from `main/CMakeLists.txt`.
- MINIMAME (16 MB): launcher + 16 OTA app slots + two data partitions, generated by a script.

## Toolchain

ESP-IDF **v5.3.4** via Docker, no host IDF:

```sh
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 idf.py -B build_docker build
esptool --chip esp32c6 --port /dev/cu.usbmodem2101 --baud 921600 write_flash @build_docker/flash_args
```

Flashing runs on the host (Docker Desktop cannot reach USB). The image is already pulled.

## Main loop and task structure in the references

- PELLETINO: one task (`app_main`, prio 1, 3584 stack). Loop: emulate 1-3 frames, render bands
  inline (audio pumped 6x per frame from inside the flush), poll input, `vTaskDelay` remainder.
- MINIMAME games: `app_main` (prio 1) emulates; `render` task (prio 6, 4096) converts and pushes
  strips; two 8-bit framebuffers in queues; dropped frames counted, never stalled.
- NESTOR: `app_main` (prio 1, 8192) does everything; `pad` task + NimBLE tasks for BLE; the
  blocking I2S write paces the loop; frameskip when the DAC queue drains below 3 of 5 descriptors.

## Files to lift

| Purpose | Take from | Notes |
|---|---|---|
| ST7789 driver | `NESTOR/components/display/` | landscape + indexed strip push; generalize `game_w` to 280 |
| BLE gamepad | `NESTOR/components/ble_pad/` + `NESTOR/components/esp_hid/` | wholesale; edit `map_buttons()` only |
| Buttons, BAT_EN, battery ADC | `NESTOR/main/medal.c` + `medal.h` (98 lines) | smallest complete version |
| ES8311 + I2S | `NESTOR/components/audio_hal/` for the C API, feeding model from `PELLETINO/components/audio_hal/src/audio_hal.cpp:196-254` | DAC-paced pull |
| Heap logging | `NESTOR/main/main.c:115` `log_heap()` | free, largest block, min ever |
| Partition mmap | `NESTOR/main/main.c:69-103` `roms_init()` | map only the used span |
| sdkconfig.defaults | `NESTOR/sdkconfig.defaults` | already has BLE + 16 MB; set `PM_ENABLE=n` |
| Build/flash scripts | `NESTOR/build.sh`, `NESTOR/flash.sh` | |
