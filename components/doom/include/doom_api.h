#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Runs Doom forever on the calling task. wad points at the memory-mapped doom1.whd. */
void doom_run(const uint8_t *wad);
#ifdef __cplusplus
}
#endif
