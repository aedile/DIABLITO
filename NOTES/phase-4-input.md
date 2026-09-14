# Phase 4: Input

Date: 2026-09-14. Logs: `NOTES/logs/phase4-run1.log` (hung, see below), `phase4-run2.log`
(the full scripted run).

## What was built

- `components/ble_pad/` and `components/esp_hid/` lifted wholesale from NESTOR. Only edits:
  `map_buttons()` now exposes X, Y, LB, RB as `PAD_X/Y/L/R` (Xbox BLE numbering 4, 5, 7, 8),
  the NVS namespace is `diablito`, and the per-advertisement "seen" log is demoted to debug.
- `main.c`: `nvs_flash_init()` then `ble_pad_init()` before Doom starts. With no saved
  controller the medal pairs with the first HID gamepad it sees; once one connects, reconnects
  are restricted to that address (`ble_pad_scan_any(false)` in `I_GetEvent`).
- `components/doom/esp/i_input.c`: a virtual-key layer. Each pad button or medal gesture is a
  bit; on edge it posts a fixed set of Doom key codes (down or up). Doom only understands keys,
  so one button carries both its in-game and its menu meaning:

| Input | Doom keys | Meaning |
|---|---|---|
| D-pad / left stick | arrows | move, turn, menu navigation |
| A | RCTRL, Enter, y | fire, menu forward, confirm prompts |
| B | space, Backspace, n | use, menu back, decline prompts |
| X | RALT | strafe modifier |
| Y | RSHIFT | run |
| LB / RB | [ / ] | previous / next weapon (`key_prevweapon/nextweapon` bound at init) |
| Menu (Start) | Escape | open/close menu; any key also opens the menu from the title/demo |
| View (Select) | Tab | automap |
| BOOT held | RCTRL, Down | fire; menu down; opens menu from attract |
| PWR tap | space, Enter, y | use; menu forward; confirm |
| PWR held 0.6 s | Escape | menu |
| PWR held 3 s | | power off (`medal.c`, was 2 s in NESTOR) |

- Bench pad: keys typed into the USB serial console act as pad buttons held for 120 ms
  (`w s a d` d-pad, `j k u i o p` A B X Y L R, `q` Start, `e` Select, `l n h` medal BOOT /
  PWR tap / PWR hold). `tools/monitor.py --script "20:q,23:j"` sends them on a schedule. This
  is what lets the menu and gameplay path be verified without hands on the device.

## The hang in run 1

Installing `usb_serial_jtag_driver_install()` without `esp_vfs_usb_serial_jtag_use_driver()`
left `printf` on the ROM polling path while the driver's ISR serviced the same FIFO. The first
received byte started the fight; the device wedged 43 s in (frame 1151, output stopped, tics
stopped). Fixed by switching the console VFS to the driver in `I_InputInit`.

## Measured (run 2, `phase4-run2.log`)

Heap around BLE start (phase 4 build: 64 KB zone, list buffer on heap, 16 KB main stack):

| Point | Free heap | Largest block |
|---|---|---|
| Boot | 293,380 | 262,144 |
| After display + WHD mmap | 273,264 | 245,760 |
| **After BLE init** (NimBLE controller + host + HID host + pad task, scanning) | 234,392 | 208,896 |
| In game (E1M1, BLE scanning) | 67,708 | 41,984 |

So the BLE stack costs about 39 KB of heap at runtime plus 43 KB of `.bss`. Doom's own heap
footprint is unchanged at ~166 KB (framebuffers 107.5 KB + list buffer 47 KB + misc).
`_bss_end` 0x408341D0, 48,688 bytes short of the short-pointer limit.

Zone free: 40,564 minimum of 65,536 (25 KB peak in E1M1 and the demos). Main task stack:
14,540 of 16,384 free.

Scripted session (host time -> device response):

| t | Sent | Device |
|---|---|---|
| 20 s | Start | `input: down 0x40`, menu opens over demo 1 |
| 23 s | A | New Game -> skill menu |
| 26 s | A | Hurt Me Plenty -> E1M1 loads |
| 34-35 s | Up x5 | player walks |
| 36 s | | `mem @frame 1024: ... demo 0, usergame 1` (a real game is running) |
| 36-38 s | A, A, B | fire, fire, use |
| 40, 42 s | Select x2 | automap on, off |
| 45-48 s | Left x3, Up x4 | turn, walk |
| 50-57 s | Start, Down x5, A | menu, down to Quit Game, Enter |
| 57 s | (A also sends y) | `I_Quit: restarting` -> `rst:0xc (SW_CPU)` -> back in attract at 60 s |

Every keypress in the script produced an `input: down` line; no drops, no resets other than the
requested quit. Frame times in E1M1 with BLE scanning were in the same 35-50 ms band as the
demos (the frame lines are in the log).

## Gate status

- Menu navigable with buttons only: the medal button path posts the same virtual keys the
  script drove (BOOT = down/fire, PWR tap = Enter, PWR hold = Escape). The key mapping is
  proven; the physical GPIO edge logic in `medal_vkeys()` still needs a thumb on the buttons.
- E1M1 playable start to exit with the gamepad: the Doom side (start, move, fire, use, map,
  quit) is proven through the bench pad. The BLE HID path (pairing, bonding, report parsing)
  is NESTOR's shipped code, unchanged except the button map, and needs an Xbox BLE controller
  in the room. Scanning is confirmed alive: the log shows advertisements from nearby devices.

## Hands-on session (Jesse, Xbox Wireless Controller)

Three real problems surfaced and were fixed, in order (logs `phase4-sticks-jesse*.log`):

1. **Stale bond.** Putting the pad back into pairing mode drops its bond; the medal still held
   its copy, encryption failed with `BLE_HS_ENOTCONN` (7) as the pad dropped the link, and the
   HID host then waited forever in service discovery on a dead connection. Fix: bounded GATT
   waits, `ENOTCONN` handled in the open path, and on a failed open of the saved pad the medal
   forgets it and pairs fresh with no 60 s penalty. Pairing now takes about four seconds.
2. **HID events serviced too slowly.** The `esp_hidh` event task inherited the game task's
   priority; Doom never yields, so the pad's 100 Hz reports queued up (5-deep queue, host task
   blocking on the post) and the game saw stick values seconds late. Fix: event task priority 5,
   queue 16, and the per-notification INFO log in the host task removed.
3. **This pad's left-stick Y is faulty.** Raw report bytes: at rest `e1 25` (0x25E1, 15% of
   travel, i.e. 70% forward) while X and both right-stick axes sit at 0x8000; after a push it
   reads `00 00` and stays there through six reports as X returns to centre; after a pull,
   `ff ff` likewise. Every build had faithfully turned that into permanent forward motion. The
   left stick is now disabled (`LEFT_STICK_MOVE 0`); the analog movement path (Doom's mouse
   forward/turn) is written and one define away for a pad with a working axis.

Final mapping: d-pad moves and strafes (arrows in menus), right stick turns proportionally,
triggers or A fire, B use, Menu opens the menu, View toggles the map; BOOT/PWR as in the table
above; BOOT held 10 s forgets the controller.

**Phase 4 gate: met.** Jesse played E1M1 with the pad and the medal buttons ("feels good");
the bench-pad session above covers start-to-exit including the quit path.
