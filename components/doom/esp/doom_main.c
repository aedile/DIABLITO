// Entry point glue: main/main.c hands us the mapped WHD and we run D_DoomMain on the caller's
// task. Replaces src/i_main.c.
#include "pico.h"
#include "esp_log.h"
#include "doomtype.h"
#include "m_argv.h"
#include "i_system.h"
#include "doom_api.h"

void D_DoomMain(void);
extern const uint8_t *whd_map_base;
void W_Memory_SetBase(const uint8_t *base);

void doom_run(const uint8_t *wad)
{
    W_Memory_SetBase(wad);
    I_Init();
    D_DoomMain();
}
