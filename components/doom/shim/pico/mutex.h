#pragma once
#include "pico.h"
typedef struct { int owned; } mutex_t;
static inline void mutex_init(mutex_t *m) { m->owned = 0; }
static inline void mutex_enter_blocking(mutex_t *m) { m->owned = 1; }
static inline void mutex_exit(mutex_t *m) { m->owned = 0; }
