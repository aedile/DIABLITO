# Disabled, stubbed, or cut

Running list. Every entry says what, why, and what it would take to restore.

| Item | Status | Why | To restore |
|---|---|---|---|
| OPL2 music (`USE_EMU8950_OPL`, emu8950) | deferred, compile flag off | RP2040 ran the synth on its second core; we have one core at 160 MHz | measure headroom after phase 6, then try `EMU8950_LINEAR` C path at 22050 Hz |
| Super-tiny WHX format / `DEMO1_ONLY` | not used | 16 MB flash makes WHX pointless and `DEMO1_ONLY` breaks the attract loop | n/a |
| USB keyboard (TinyUSB host) | removed | no USB host on this board's use case | n/a |
| I2C multiplayer (`piconet`) | removed | no second device | n/a |
| RP2040 interpolator paths (`USE_INTERP`) | off, C fallbacks | no such hardware | RISC-V hand tuning in phase 6 if profiling points there |
| ARM asm (`blit.S`, `m_fixed.h`, `doomtype.h`) | C fallbacks | RISC-V | phase 6 |
| Host `whd_gen` clang error | worked around with `-Wno-missing-template-arg-list-after-template-kw` | new clang diagnostic on upstream C++ | none needed |
| Savegames (`P_SaveGameWriteFlashSlot`) | stubbed: no slots, writes fail with Doom's own "not enough space" prompt | RP2040 code wrote raw XIP flash sectors | write slots into the `saves` partition with `esp_partition_write` (phase 7 candidate) |
| Left-stick analog movement (`LEFT_STICK_MOVE`) | off | the test pad's left-stick Y axis rests at 70% and latches at the ends (raw bytes in `phase4-sticks-jesse9.log`) | flip the define for a pad with a healthy axis |
| Two-button game navigation (BOOT fire, PWR Enter/Escape) | removed 2026-09-15 | Jesse: physical buttons are power, mute and forget-controller only | the key table entries are in git history (`b564cfb`) |
| Text mode, ENDOOM, DOS-prompt exit screen | removed | needed the scanvideo text renderer; medal quits by rebooting into attract | n/a |
| Doom's own `-mb`, config file, arguments | compiled out (`NO_USE_ARGS`, `NO_USE_SAVE_CONFIG`) | no filesystem | n/a |
