# Network play: feasibility

Date: 2026-09-19. Log: `NOTES/logs/nettest-wifi-ram.log`.

## Wi-Fi (ESP-NOW) does not fit next to BLE and Doom

Measured with a throwaway build that linked `esp_wifi` + `esp_now` with minimum buffer counts
(2 static RX, 4 dynamic RX/TX, AMPDU off, no NVS) alongside the normal firmware:

| | Normal build | + Wi-Fi/ESP-NOW linked |
|---|---|---|
| Image | 1.09 MB | 1.49 MB |
| `_bss_end` | 0x40836668 (39 KB inside the short-pointer window) | 0x40847308 (**29 KB past it**) |
| Heap after display + WHD mmap, before any radio init | 273 KB | 195 KB (-78 KB) |

Linking the stack alone moves 68 KB of IRAM code and statics in front of `.bss` and takes 78 KB
from the boot heap, before `esp_wifi_init()` allocates anything. The running game has 36 KB free.
The boot-time window check aborted, as designed. Making room would mean giving up a framebuffer
(107 KB for the pair) or BLE, and BLE is the controller.

## What does fit: a BLE link between two boards

NimBLE is already resident, with the peripheral role compiled in (the HID host quirk). A second
connection costs a few KB. Doom's lockstep traffic is tiny: one ticcmd per player per tic, about
10 bytes at 35 Hz each way. A 7.5-15 ms connection interval is well inside a 28 ms tic. Upstream's
`piconet.c` (936 lines, host/client lockstep with a lobby in Doom's menu under `NET_MENU`, built
for a 1 Mbps I2C bus and tiny RAM) is the protocol to adapt; only its transport changes.

Open questions to measure when two boards are on the bench: radio time-sharing with a 100 Hz
gamepad on each side, heap with two connections, and lockstep stalls when one side drops a frame
(both boards run ~30 FPS, tics are 35 Hz).
