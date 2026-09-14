/* pico/sem.h shim over FreeRTOS counting semaphores. The game task and the display task
 * hand framebuffers back and forth through render_frame_ready / display_frame_freed exactly
 * as core 0 and core 1 did on the RP2040; the core1_* semaphores are initialised but idle. */
#pragma once
#include "pico.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
typedef struct { SemaphoreHandle_t h; } semaphore_t;
static inline void sem_init(semaphore_t *s, int16_t initial, int16_t max) { s->h = xSemaphoreCreateCounting(max, initial); }
static inline bool sem_available(semaphore_t *s) { return uxSemaphoreGetCount(s->h) > 0; }
static inline bool sem_release(semaphore_t *s) { return xSemaphoreGive(s->h) == pdTRUE; }
static inline void sem_acquire_blocking(semaphore_t *s) { xSemaphoreTake(s->h, portMAX_DELAY); }
static inline bool sem_acquire_timeout_ms(semaphore_t *s, uint32_t ms) { return xSemaphoreTake(s->h, pdMS_TO_TICKS(ms)) == pdTRUE; }
static inline bool sem_try_acquire(semaphore_t *s) { return xSemaphoreTake(s->h, 0) == pdTRUE; }
