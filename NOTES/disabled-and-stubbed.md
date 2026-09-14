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
