// Device-side extras: persistent settings, battery gauge and cutoff, and the Konami code.
// Everything here runs on the game task, polled from I_GetEvent (once per tic).
#include "pico.h"
#include "esp_timer.h"
#include "nvs.h"
#include "doomtype.h"
#include "d_event.h"
#include "doom/doomstat.h"
#include "doom/m_menu.h"
#include "ble_pad.h"
#include "doom_api.h"

void doom_toast(const char *line1, const char *line2);
void doom_power_off(void);
void M_SetSkillCursor(int skill);

// ---- persistent settings: Doom's own option variables, mirrored to NVS when they change ----
extern int sfxVolume, musicVolume;
extern isb_int8_t mouseSensitivity, showMessages, usegamma;
typedef struct { uint8_t version, sfx, music, sensitivity, messages, gamma, skill; } settings_t;
#define SETTINGS_VERSION 1
static settings_t saved;

static settings_t settings_now(void)
{
    settings_t s = { SETTINGS_VERSION, sfxVolume, musicVolume, mouseSensitivity, showMessages, usegamma, saved.skill };
    if (usergame && gamestate == GS_LEVEL) s.skill = gameskill;
    return s;
}

void extras_load_settings(void)      // before D_DoomMain: S_Init and the menus pick these up
{
    nvs_handle_t h; settings_t s; size_t len = sizeof s;
    saved = (settings_t){ SETTINGS_VERSION, 8, 8, 5, 1, 0, sk_medium };
    if (nvs_open("medal", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "settings", &s, &len) == ESP_OK && len == sizeof s && s.version == SETTINGS_VERSION &&
            s.sfx <= 15 && s.music <= 15 && s.sensitivity <= 9 && s.gamma <= 4 && s.skill <= sk_nightmare) saved = s;
        nvs_close(h);
    }
    sfxVolume = saved.sfx; musicVolume = saved.music; mouseSensitivity = saved.sensitivity;
    showMessages = saved.messages; usegamma = saved.gamma;
    M_SetSkillCursor(saved.skill);
    printf("settings: sfx %d music %d turn %d messages %d gamma %d skill %d\n", saved.sfx, saved.music,
           saved.sensitivity, saved.messages, saved.gamma, saved.skill);
}

static void settings_poll(int64_t now)
{
    static int64_t next;
    if (now < next) return;
    next = now + 2000000;                      // a slider drag settles before we write flash
    settings_t s = settings_now();
    if (!memcmp(&s, &saved, sizeof s)) return;
    nvs_handle_t h;
    if (nvs_open("medal", NVS_READWRITE, &h) == ESP_OK) { nvs_set_blob(h, "settings", &s, sizeof s); nvs_commit(h); nvs_close(h); }
    saved = s;
    printf("settings saved: sfx %d music %d turn %d messages %d gamma %d skill %d\n", s.sfx, s.music, s.sensitivity, s.messages, s.gamma, s.skill);
}

// ---- battery: gauge toast on demand, low warning, cutoff ----
static int (*battery_mv)(void), (*battery_pct)(void);
void doom_set_battery_hooks(int (*mv)(void), int (*pct)(void)) { battery_mv = mv; battery_pct = pct; }

void extras_battery_toast(void)
{
    if (!battery_mv) return;
    char l1[24], l2[24];
    int mv = battery_mv(), pct = battery_pct();
    snprintf(l1, sizeof l1, "Battery %d%%", pct);
    snprintf(l2, sizeof l2, "%d.%02d V", mv / 1000, (mv % 1000) / 10);
    doom_toast(l1, l2);
    printf("battery: %d%% %d mV\n", pct, mv);
}

// ponytail: 15 % warning and 3.30 V cutoff are MINIMAME's numbers for this cell and board; the
// percent curve in medal.c is linear 3.3-4.2 V. Tune here if a pack behaves differently.
#define BATTERY_LOW_PCT 15
#define BATTERY_CUTOFF_MV 3300
static void battery_poll(int64_t now)
{
    static int64_t next, next_warn; static int strikes;
    if (!battery_mv || now < next) return;
    next = now + 10000000;
    int mv = battery_mv(), pct = battery_pct();
    if (mv <= 0) return;
    if (mv < BATTERY_CUTOFF_MV) {
        if (++strikes >= 3) { printf("battery %d mV: cutoff\n", mv); doom_toast("Battery empty", "powering off"); sleep_ms(1500); doom_power_off(); }
    } else strikes = 0;
    if (pct <= BATTERY_LOW_PCT && now > next_warn) {
        next_warn = now + 300000000;             // every five minutes
        char l2[24]; snprintf(l2, sizeof l2, "%d%%, charge soon", pct);
        doom_toast("Battery low", l2);
    }
}

// ---- Konami code: up up down down left right left right B A -> IDDQD + IDKFA ----
// The cheats are typed into Doom's own cheat parser, one character per tic (the event queue is
// eight deep), so they behave exactly like the keyboard cheats, toggle included.
static const char *inject;
static int konami_report;          // tics until the cheat result is logged (the events have to be eaten first)
static void inject_poll(void)
{
    if (konami_report && !--konami_report)
        printf("konami: god %d, shotgun %d, chainsaw %d, blue card %d\n", !!(players[consoleplayer].cheats & CF_GODMODE),
               players[consoleplayer].weaponowned[wp_shotgun], players[consoleplayer].weaponowned[wp_chainsaw], players[consoleplayer].cards[it_bluecard]);
    if (!inject) return;
    event_t ev = { .type = ev_keydown, .data1 = *inject, .data2 = *inject, .data3 = *inject };
    D_PostEvent(&ev);
    ev.type = ev_keyup;
    D_PostEvent(&ev);
    if (!*++inject) { inject = NULL; konami_report = 4; }
}

static void konami_poll(uint32_t pad_down, bool in_level)
{
    static const uint16_t code[10] = { PAD_UP, PAD_UP, PAD_DOWN, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_LEFT, PAD_RIGHT, PAD_B, PAD_A };
    static int at;
    const uint32_t watched = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_A | PAD_B;
    if (!in_level) { at = 0; return; }
    pad_down &= watched;
    if (!pad_down) return;
    if (pad_down == code[at]) at++; else at = pad_down == code[0] ? 1 : 0;
    if (at == 10) {
        at = 0;
        inject = "iddqdidkfa";
        doom_toast("Konami code!", "god mode + all weapons");
        printf("konami code\n");
    }
}

void extras_poll(uint32_t pad_down, bool in_level)
{
    int64_t now = esp_timer_get_time();
    inject_poll();
    konami_poll(pad_down, in_level);
    settings_poll(now);
    battery_poll(now);
}
