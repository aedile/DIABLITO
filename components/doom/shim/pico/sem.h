/* pico/sem.h shim. Everything runs on one thread in sequence, so a semaphore is a counter that
 * can never block: an acquire on an empty semaphore is a no-op, because nothing else could ever
 * release it. The render/display handshake is driven explicitly in pd_render.cpp / i_video.c. */
#pragma once
#include "pico.h"
typedef struct { int16_t permits, max_permits; } semaphore_t;
static inline void sem_init(semaphore_t *s, int16_t initial, int16_t max) { s->permits = initial; s->max_permits = max; }
static inline bool sem_available(semaphore_t *s) { return s->permits > 0; }
static inline bool sem_release(semaphore_t *s) { if (s->permits < s->max_permits) { s->permits++; return true; } return false; }
static inline void sem_acquire_blocking(semaphore_t *s) { if (s->permits > 0) s->permits--; }
static inline bool sem_acquire_timeout_ms(semaphore_t *s, uint32_t ms) { (void)ms; if (s->permits > 0) { s->permits--; return true; } return false; }
static inline bool sem_try_acquire(semaphore_t *s) { return sem_acquire_timeout_ms(s, 0); }
