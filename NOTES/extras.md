# Extras: Konami code, persistent settings, battery gauge (2026-09-19)

All in `components/doom/esp/extras.c`, polled once a tic from `I_GetEvent`.

| Feature | How | Measured on the device (`logs/extras-bench*.log`) |
|---|---|---|
| Konami code | raw d-pad/B/A press edges, in a level only; types `iddqdidkfa` into Doom's own cheat parser one character a tic (event queue is 8 deep) | `konami code` then `konami: god 1, shotgun 1, chainsaw 1, blue card 1` |
| Persistent settings | NVS blob `medal/settings` {sfx, music, turn sensitivity, messages, gamma, last skill}; compared every 2 s, written only on change; loaded before `D_DoomMain` | sliders moved from the bench pad: `settings saved: sfx 6 music 7 ...`, after reset `settings: sfx 6 music 7 turn 5 messages 0 gamma 0 skill 2` |
| Volume sliders | Doom's own Sound Volume menu, already worked; now remembered | as above |
| Battery gauge | PWR tap (30 ms to 1 s) shows a toast; bench key `n` | `battery: 97% 4176 mV` |
| Low warning / cutoff | toast every 5 min at <= 15 %; power off after three 10 s readings under 3300 mV | **not exercised**: needs a run-down battery. Thresholds are MINIMAME's. |

Confirmed by hand by Jesse 2026-09-19: physical PWR-tap battery toast, Konami code entry on the pad, its toast,
its effect (weapons via LB/RB), and the single shot on the final A. Still open: the low warning and cutoff
path, to be tried with a smaller battery.
The last press of the code is A, so the gun fires once when the code lands.
