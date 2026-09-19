#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Runs Doom forever on the calling task. wad points at the memory-mapped doom1.whd. */
void doom_run(const uint8_t *wad);
/* Battery readers (millivolts, percent) for the gauge toast, low warning and cutoff. */
void doom_set_battery_hooks(int (*mv)(void), int (*pct)(void));
#ifdef __cplusplus
}
#endif
