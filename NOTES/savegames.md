# Savegames

Date: 2026-09-19. Logs: `NOTES/logs/saves-run1.log`, `saves-run2-after-reboot.log`.

Upstream keeps compressed saves (`SAVE_COMPRESSED`, Huffman bit stream built in the renderer's
work area) in raw XIP flash below the end of the chip, packed and shuffled with a 4 KB sector
buffer. Here the `saves` partition (128 KB, subtype 0x41) holds **eight fixed 16 KB slots**, each
`[magic "DVSG"][size][data]`:

- The partition is memory-mapped once, so `P_SaveGameGetExistingFlashSlotAddresses()` hands Doom
  pointers into flash and loading reads the slot in place, as on the RP2040.
- `P_SaveGameWriteFlashSlot()` is one `esp_partition_erase_range` of the slot plus the data write,
  with the header written last so a torn write leaves an empty slot. A NULL buffer clears a slot
  (Doom's "clear this slot?" prompt). It runs inside upstream's `pd_start_save_pause()` /
  `pd_end_save_pause()` so the display shows the saving state and sound fades around the flash
  operation.
- A save larger than 16 KB - 8 is refused and Doom shows its own "not enough space" prompt.
- No keyboard: `M_SaveSelect` always takes Chocolate Doom's joystick path, which names the save
  after the map, so A on a slot then A again saves.

Measured: an E1M1 save is 1,436 bytes. Scripted run: new game, move, Save Game to slot 0
(`saves: slot 0 written, 1436 bytes (ESP_OK)`), play on, Load Game restores it. After a reboot,
Load Game from the attract menu restores the same save (`LOAD GAME slot 0, 1436 bytes`, then
`demo 0, usergame 1`). Heap unchanged at 36.5 KB free.
