/* pico.h shim: the slice of pico-sdk's platform header that rp2040-doom's sources use, mapped
 * onto ESP-IDF. Anything RP2040-hardware-specific is deliberately absent. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/cdefs.h>

#ifndef PICO_ON_DEVICE
#define PICO_ON_DEVICE 1
#endif
#define PICO_RP2040 0
#define PICO_RP2350 0
#define PICO_NO_HARDWARE 0

typedef unsigned int uint;

#define __not_in_flash_func(f) f
#define __no_inline_not_in_flash_func(f) __attribute__((noinline)) f
#define __time_critical_func(f) f
#define __scratch_x(n)
#define __scratch_y(n)
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef __noinline
#define __noinline __attribute__((noinline))
#endif
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#define __force_inline inline __attribute__((always_inline))
#define __compiler_memory_barrier() __asm__ volatile("" ::: "memory")
#define __breakpoint() abort()
#define tight_loop_contents() ((void)0)
#define hard_assert assert
#define invalid_params_if(x, test) assert(!(test))
#ifndef __STRING
#define __STRING(x) #x
#endif
#ifndef __XSTRING
#define __XSTRING(x) __STRING(x)
#endif
#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define panic(...) do { printf("PANIC: " __VA_ARGS__); printf("\n"); fflush(stdout); abort(); } while (0)
#define get_core_num() 0
#define panic_unsupported() panic("unsupported")

/* pico-sdk debug pin macros (CU_REGISTER_DEBUG_PINS etc.) -> nothing */
#define CU_REGISTER_DEBUG_PINS(...)
#define CU_SELECT_DEBUG_PINS(...)
#define DEBUG_PINS_SET(g, m) ((void)0)
#define DEBUG_PINS_CLR(g, m) ((void)0)
#define DEBUG_PINS_XOR(g, m) ((void)0)

/* scanvideo's pixel macro, redefined as RGB565 byte-swapped for the ST7789 wire order */
#define PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b) \
    ((uint16_t)__builtin_bswap16((uint16_t)(((((r) >> 3) & 31) << 11) | ((((g) >> 2) & 63) << 5) | (((b) >> 3) & 31))))

#ifdef __cplusplus
extern "C" {
#endif
uint32_t time_us_32(void);
uint64_t time_us_64(void);
void sleep_ms(uint32_t ms);
void busy_wait_us(uint64_t us);
static inline void sleep_us(uint64_t us) { busy_wait_us(us); }
#ifdef __cplusplus
}
#endif

/* hardware/sync.h spin locks and platform intrinsics: single core, nothing to lock */
typedef struct { int dummy; } spin_lock_t;
static inline spin_lock_t *spin_lock_instance(uint n) { static spin_lock_t locks[32]; return &locks[n & 31]; }
static inline uint32_t spin_lock_blocking(spin_lock_t *l) { (void)l; return 0; }
static inline void spin_unlock(spin_lock_t *l, uint32_t save) { (void)l; (void)save; }
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }
#define __mul_instruction(a, b) ((a) * (b))
