# Phase 7: Medal mode

Date: 2026-09-14. Log: `NOTES/logs/phase7-run1-idle.log`.

## What was built

- **Boot straight into attract.** Doom's own title/demo cycle is the attract loop; it starts
  about 2.3 s after power-on, preceded by a 1.5 s DIABLITO splash drawn in `main.c` (5x7 glyphs
  scaled 5x, Doom red, no assets). Any key on the title or during a demo opens the menu, so a
  button press or a pad button leaves attract.
- **Idle display sleep: off by default.** Jesse wants the medal visual in attract, so
  `doom_idle_sleep_s` defaults to 0. The mechanism is built and verified: after that many seconds without any input while a demo is playing, the panel gets backlight off plus
  ST7789 `SLPIN`, and the game task parks polling the two buttons and the pad at 10 Hz. Tics
  stop, the sound ring drains to silence, BLE keeps listening. Any medal button or pad button
  wakes it (`SLPOUT`, backlight back). NVS key `medal/idle_s` or `idf.py -DIDLE_SLEEP_S=n build`
  turns it on.
- **Battery run log.** Every 60 s the uptime and battery millivolts go to NVS (`medal/runlog`);
  at the next boot the previous run's last record is printed:
  `previous run (boot N) lasted S s (M min), last battery X mV`. That is how the runtime on a
  full charge gets measured without a cable attached.
- **Mute: BOOT held 3 s** toggles it (like the other medals), persisted in NVS `medal/mute`,
  announced on Doom's HUD line ("SOUND MUTED" / "SOUND ON"). Muted output is silence at the same
  sample rate so the DAC still paces the mixer. BOOT held 10 s still forgets the controller.
- Power off: PWR held 3 s (`medal.c`, BAT_EN low). On USB the rail stays up and the medal just
  goes dark.

- **Portrait.** Jesse wants the medal worn portrait, so `DISPLAY_PORTRAIT` (default 1 in
  `display.h`) runs the panel at 240x280 (MADCTL 0x00, the 20 px panel offset on the row axis)
  with Doom scaled 4:3 horizontally to 240x200 (columns 3 and 7 of every 8 dropped) and 40 px
  black bars top and bottom. Landscape (280x240, 8:7, 20 px bars) is the `#else`. Wire time per
  frame drops from 11.7 to 9.9 ms; demo frames 28.8 ms mean (34.7 FPS).

## Measured

- Idle sleep triggered at 120 s of untouched attract loop, frame output stopped, heap steady at
  46,412, no reset (`phase7-run1-idle.log`).
- Splash and wake-on-button: Jesse's eyes and thumb (pending).
- **Battery runtime, display on:** pending. Protocol: charge fully, unplug, leave it in the attract loop until it dies, plug back in, boot, read the
  "previous run lasted" line.

Gate: open until the runtime number is in.
