// Timer functions for the ESP32-C6 build. Also hosts the pico-sdk time shims from pico.h.
#include "pico.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i_timer.h"
#include "doomtype.h"

uint32_t time_us_32(void) { return (uint32_t)esp_timer_get_time(); }
uint64_t time_us_64(void) { return (uint64_t)esp_timer_get_time(); }
void busy_wait_us(uint64_t us) { esp_rom_delay_us(us); }
void sleep_ms(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t ? t : 1);
}

// returns time in 1/35th second tics
int I_GetTime(void)
{
    return (int)(time_us_64() * TICRATE / 1000000);
}

int I_GetTimeMS(void)
{
    return (int)(time_us_64() / 1000);
}

void I_Sleep(int ms) { sleep_ms(ms); }

void I_WaitVBL(int count) { I_Sleep((count * 1000) / 70); }

void I_InitTimer(void) {}
