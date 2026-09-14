// Sampling profiler: a 2 kHz ISR-dispatched timer records the interrupted PC (mepc) into a small
// hash of 32-byte buckets; every 20 s the hottest buckets are printed for tools/symbolize.sh.
// Costs a few microseconds per sample; enabled only when DOOM_PROFILE is defined.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_attr.h"
#include "riscv/rv_utils.h"

#define BUCKETS 1024
static DRAM_ATTR uint32_t pc_key[BUCKETS];
static DRAM_ATTR uint32_t pc_cnt[BUCKETS];
static DRAM_ATTR uint32_t samples, dropped;

static void IRAM_ATTR on_tick(void *arg)
{
    uint32_t pc = RV_READ_CSR(mepc) >> 5;
    samples++;
    uint32_t h = (pc * 2654435761u) >> 22;      // 10 bits
    for (int i = 0; i < 8; i++) {
        uint32_t k = (h + i) & (BUCKETS - 1);
        if (pc_key[k] == pc) { pc_cnt[k]++; return; }
        if (pc_key[k] == 0) { pc_key[k] = pc; pc_cnt[k] = 1; return; }
    }
    dropped++;
}

void prof_start(void)
{
    const esp_timer_create_args_t a = { .callback = on_tick, .dispatch_method = ESP_TIMER_ISR, .name = "prof" };
    esp_timer_handle_t t;
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_periodic(t, 500);
}

void prof_report(void)
{
    // top 40 by count, printed as "pc count" (pc is the bucket start)
    uint32_t total = samples;
    printf("prof: %lu samples, %lu dropped\n", (unsigned long)total, (unsigned long)dropped);
    for (int n = 0; n < 40; n++) {
        int best = -1;
        for (int i = 0; i < BUCKETS; i++) if (pc_key[i] && (best < 0 || pc_cnt[i] > pc_cnt[best])) best = i;
        if (best < 0 || pc_cnt[best] == 0) break;
        printf("prof: %08lx %lu %.1f%%\n", (unsigned long)(pc_key[best] << 5), (unsigned long)pc_cnt[best], 100.0 * pc_cnt[best] / total);
        pc_cnt[best] = 0;
    }
    memset(pc_key, 0, sizeof pc_key); memset(pc_cnt, 0, sizeof pc_cnt); samples = dropped = 0;
}
