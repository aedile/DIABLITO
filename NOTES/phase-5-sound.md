# Phase 5: Sound

Date: 2026-09-14. Logs: `NOTES/logs/phase5-sound-run1.log` (first run, ring accounting off by
one descriptor), `phase5-sound-run2.log` (fixed).

## What was built

`components/doom/esp/i_sound_glue.c` implements the pico-extras audio buffer-pool API that the
untouched RP2040 mixer (`src/pico/i_picosound.c`: 8 channels, ADPCM blocks of 249 samples,
16:16 resampling, low-pass, stereo pan) already talks to:

- ES8311 codec over I2C at 100 kHz with PELLETINO's proven register sequence; I2S standard mode
  from NESTOR's HAL. 16-bit stereo (the mixer writes interleaved L/R) at **22050 Hz**
  (`PICO_SOUND_SAMPLE_FREQ` made overridable in `i_picosound.h`; the RP2040 used 44100 for
  effects-only builds, 49716 with OPL music). Doom's effects are 11025 Hz samples, so 22050 keeps
  every sample and halves the mixing work.
- DMA ring: 8 descriptors x 256 stereo frames = 8,192 bytes, 92 ms. `take_audio_buffer()` hands
  the mixer one of its two 1,024-frame buffers only when the ring can take all of it (minus the
  one descriptor the driver keeps in flight), `give_audio_buffer()` writes it with a zero timeout.
  Production is therefore locked to the DAC clock and never blocks the game task.
- The mixer runs from Doom's own `I_UpdateSound()` call sites plus once per displayed frame
  (`I_DisplayFrame`), so a 46 ms buffer is offered at least twice per 40-80 ms frame.
- OPL music stays a stub (`music_opl_module` returns no devices).

## Measured

| Point | Value |
|---|---|
| Heap free in E1M1, BLE + sound | 54,400 (largest block 28,672); sound cost ~13 KB (DMA ring, I2S and I2C drivers) |
| Queue depth during menu, demo, E1M1 with pistol fire | 46 to 69 ms of 92 ms |
| Underruns | 1, at channel start (preload of silence outruns the first mixer buffer); never grows over the runs |
| Short writes after the accounting fix | none |

Frame time in the 3D view, same scripted E1M1 session with pistol fire:

| Build | Frames | Mean | Min | Max | FPS |
|---|---|---|---|---|---|
| Phase 4, silent sink | 2,469 | 38.5 ms | 12.6 | 54.8 | 26.0 |
| Phase 5, mixer + I2S | 1,719 | 39.1 ms | 12.6 | 50.8 | 25.6 |

Mixing costs about 0.6 ms per frame (1.5%). No frame-rate collapse.

## Gate

- Shotgun and door sounds audible: the pistol was fired and doors used in the scripted run
  with the queue healthy; whether it is audible and clean from the speaker is Jesse's call.
- Without frame rate collapse: yes, 25.6 vs 26.0 FPS.

Phase 5 gate: measurements pass; awaiting Jesse's ears.
