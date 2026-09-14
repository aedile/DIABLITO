#pragma once
#include "pico.h"
/* RP2040 hardware divider -> native RISC-V div */
static inline uint32_t hw_divider_u32_quotient_inlined(uint32_t a, uint32_t b) { return a / b; }
static inline int32_t hw_divider_s32_quotient_inlined(int32_t a, int32_t b) { return a / b; }
static inline uint32_t hw_divider_u32_remainder_inlined(uint32_t a, uint32_t b) { return a % b; }
static inline int32_t hw_divider_s32_remainder_inlined(int32_t a, int32_t b) { return a % b; }
