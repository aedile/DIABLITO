// Input for the ESP32-C6 build: BLE gamepad (NESTOR's ble_pad) plus the medal's two buttons,
// both turned into Doom key events. Doom only ever sees keys, so a pad button is a set of key
// codes: the set covers the in-game meaning and the menu meaning at once (harmless overlap).
//
// Medal buttons are device controls only (Jesse): BOOT held 3 s = mute, 10 s = forget the
// controller; PWR tap = battery gauge toast, PWR held 3 s = power off. They never reach the game.
#include "pico.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "doomtype.h"
#include "d_event.h"
#include "doomkeys.h"
#include "i_input.h"
#include "m_controls.h"
#include "doom/doomstat.h"
#include "doom/m_menu.h"
#include "ble_pad.h"
#include "driver/usb_serial_jtag.h"
#include "display.h"
#include "nvs.h"
#include "esp_vfs_usb_serial_jtag.h"

float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int novert = 0;

#define PIN_BTN_BOOT GPIO_NUM_9
#define PIN_BTN_PWR  GPIO_NUM_18
#define PWR_OFF_HOLD_US 3000000
#define PIN_BAT_EN   GPIO_NUM_15

// virtual key set: one bit per (pad button or medal action), each posting up to 3 key codes
typedef struct { uint32_t pad_bit; uint8_t keys[3]; } vkey_t;
enum { VK_MEDAL_BOOT = 1u << 16, VK_MEDAL_PWR_TAP = 1u << 17, VK_MEDAL_PWR_HOLD = 1u << 18,
       VK_LS_UP = 1u << 20, VK_LS_DOWN = 1u << 21, VK_LS_LEFT = 1u << 22, VK_LS_RIGHT = 1u << 23,
       VK_LS_MLEFT = 1u << 24, VK_LS_MRIGHT = 1u << 25, VK_TRIGGER = 1u << 26 };
static const vkey_t vkeys[] = {
    { PAD_UP,     { KEY_UPARROW } },
    { PAD_DOWN,   { KEY_DOWNARROW } },
    { PAD_LEFT,   { KEY_LEFTARROW } },
    { PAD_RIGHT,  { KEY_RIGHTARROW } },
    { PAD_A,      { KEY_RCTRL, KEY_ENTER, 'y' } },     // fire / menu forward / confirm prompt
    { PAD_B,      { ' ', KEY_BACKSPACE, 'n' } },       // use / menu back / decline prompt
    { PAD_X,      { KEY_RALT } },                      // strafe modifier
    { PAD_Y,      { KEY_RSHIFT } },                    // run
    { PAD_L,      { '[' } },                           // previous weapon (bound below)
    { PAD_R,      { ']' } },                           // next weapon
    { PAD_START,  { KEY_ESCAPE } },                    // menu
    { PAD_SELECT, { KEY_TAB } },                       // automap
    { VK_LS_UP,    { KEY_UPARROW } },                  // left stick: move (and menu up/down)
    { VK_LS_DOWN,  { KEY_DOWNARROW } },
    { VK_LS_LEFT,  { ',' } },                          // strafe (key_strafeleft/right defaults)
    { VK_LS_RIGHT, { '.' } },
    { VK_LS_MLEFT,  { KEY_LEFTARROW } },                // left stick as a d-pad outside levels
    { VK_LS_MRIGHT, { KEY_RIGHTARROW } },
    { VK_TRIGGER,   { KEY_RCTRL } },                    // either trigger fires
};

static uint32_t prev_vk;
static bool pad_was_connected;

static void post(int type, int key)
{
    event_t ev = { .type = type, .data1 = key, .data2 = key < 128 ? key : 0, .data3 = key < 128 ? key : 0 };
    D_PostEvent(&ev);
}

static void post_set(uint32_t vk, int type)
{
    for (unsigned i = 0; i < count_of(vkeys); i++) {
        if (!(vk & vkeys[i].pad_bit)) continue;
        for (int k = 0; k < 3 && vkeys[i].keys[k]; k++) post(type, vkeys[i].keys[k]);
    }
}

// Mute: BOOT held 3 s, remembered in NVS (medal/mute), announced with a toast.
void audio_set_mute(bool m); bool audio_is_muted(void);
void doom_toast(const char *line1, const char *line2);
void extras_poll(uint32_t pad_down, bool in_level);
void extras_battery_toast(void);
static void set_mute(bool m, bool announce)
{
    audio_set_mute(m);
    nvs_handle_t h;
    if (nvs_open("medal", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "mute", m); nvs_commit(h); nvs_close(h); }
    printf("sound %s\n", m ? "muted" : "unmuted");
    if (announce) doom_toast(m ? "Muted" : "Sound on", "hold BOOT 3 s");
}

void doom_power_off(void)
{
    printf("power off\n");
    display_set_backlight(0);
    gpio_set_level(PIN_BAT_EN, 0);                   /* cuts the battery rail; on USB the rail stays up */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));        /* so sit dark until reset */
}

// Hold gestures only. A button still held from power-on must be released once before it counts
// (the medal is switched on by holding PWR). Returns the BOOT bit for the bench/idle logic; it is
// not in the key table, so nothing is posted to the game.
static uint32_t medal_vkeys(bool serial_boot)   // serial_boot: the bench pad's BOOT key counts as held too
{
    static int64_t boot_down_since, pwr_down_since;
    static bool boot_muted, boot_forgot, armed[2];
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0 || serial_boot, pwr = gpio_get_level(PIN_BTN_PWR) == 0;
    if (!armed[0]) { if (!boot) armed[0] = true; boot = false; }
    if (!armed[1]) { if (!pwr) armed[1] = true; pwr = false; }
    if (boot && !boot_down_since) { boot_down_since = now; boot_muted = boot_forgot = false; }
    if (!boot) boot_down_since = 0;
    if (boot && !boot_muted && now - boot_down_since >= 3000000) { boot_muted = true; set_mute(!audio_is_muted(), true); }
    if (boot && !boot_forgot && now - boot_down_since >= 10000000) {
        boot_forgot = true;
        ble_pad_forget(); ble_pad_scan_any(true);
        doom_toast("Controller forgotten", "pair one now");
        printf("gamepad: forgotten, pairing open\n");
    }
    if (pwr && !pwr_down_since) pwr_down_since = now;
    if (!pwr && pwr_down_since) {
        int64_t held = now - pwr_down_since;
        if (held > 30000 && held < 1000000) extras_battery_toast();      // PWR tap: battery gauge
        pwr_down_since = 0;
    }
    if (pwr && now - pwr_down_since >= PWR_OFF_HOLD_US) doom_power_off();
    return boot ? VK_MEDAL_BOOT : 0;
}

// Bench input: keys typed into the serial monitor act as a pad held for 120 ms per keystroke
// (same scheme as NESTOR): w/s/a/d d-pad, j A, k B, u X, i Y, o L, p R, q Start, e Select,
// l medal BOOT, n medal PWR tap, h medal PWR hold. tools/monitor.py --script drives this.
static uint32_t serial_vkeys(void)
{
    static const char keys[] = "wsadjkuiopqelnh";
    static const uint32_t bits[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R,
                                     PAD_START, PAD_SELECT, VK_MEDAL_BOOT, VK_MEDAL_PWR_TAP, VK_MEDAL_PWR_HOLD };
    static int64_t held_until[sizeof keys - 1];
    int64_t now = esp_timer_get_time();
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
        const char *k = memchr(keys, c, sizeof keys - 1);
        if (k) held_until[k - keys] = now + 120000;
    }
    uint32_t m = 0;
    for (unsigned i = 0; i < sizeof keys - 1; i++) if (held_until[i] > now) m |= bits[i];
    return m;
}

void I_InputInit(void)
{
    key_prevweapon = '[';
    key_nextweapon = ']';
    nvs_handle_t h; uint8_t m = 0;
    if (nvs_open("medal", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "mute", &m); nvs_close(h); }
    audio_set_mute(m);
    if (m) printf("sound muted (NVS medal/mute)\n");
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb);
    esp_vfs_usb_serial_jtag_use_driver();   // console printf must go through the same driver, or the two fight over the FIFO and hang
}

// Sticks. In a level, movement is analog through Doom's own mouse path: G_BuildTiccmd adds
// mousey to forward motion (walk 25, run 50) and 8*mousex to the turn, with the latest event per
// tic winning, so one ev_mouse per poll is exactly right. Left stick Y = forward/back (linear past
// the deadzone, full stick = run speed), right stick X = turn (squared for fine aim), left stick
// X past half travel = digital strafe. Outside a level (menus, intermission) the left stick is a
// d-pad so it can drive the menu without the mouse-motion spam M_Responder would make of it.
// ponytail: STICK_DEAD, TURN_MAX and MOVE_MAX are the tuning knobs; menu options if people differ
// LEFT_STICK_MOVE 0: the test pad's left-stick Y reports 0x25E1 at rest and latches 0000/FFFF after a
// push (raw bytes in NOTES/logs/phase4-sticks-jesse9.log), which reads as permanent forward motion.
// The other three axes are fine. Turn this on for a pad with a healthy Y axis.
#ifndef LEFT_STICK_MOVE
#define LEFT_STICK_MOVE 0
#endif
#define STICK_DEAD 8192
#define STICK_STRAFE 16384
#define TURN_MAX 160
#define MOVE_MAX 50
static int axis_scaled(int v, int dead, int max, bool squared)
{
    int mag = (v < 0 ? -v : v) - dead;
    if (mag <= 0) return 0;
    int span = 32767 - dead;
    int out = squared ? (int)((int64_t)mag * mag * max / ((int64_t)span * span)) : mag * max / span;
    return v < 0 ? -out : out;
}

#define TRIGGER_ON 9000
static uint32_t stick_vkeys(void)
{
    int16_t ax[6];
    ble_pad_axes(ax);
    uint32_t vk = 0;
    bool in_level = gamestate == GS_LEVEL && !menuactive && !demoplayback;
    if (ax[4] > TRIGGER_ON || ax[5] > TRIGGER_ON) vk |= VK_TRIGGER;
    if (in_level) {
        int turn = axis_scaled(ax[2], STICK_DEAD, TURN_MAX, true);
        int fwd = LEFT_STICK_MOVE ? -axis_scaled(ax[1], STICK_DEAD, MOVE_MAX, false) : 0;    // HID Y grows downward
        if (turn || fwd) {
            event_t ev = { .type = ev_mouse, .data1 = 0, .data2 = turn, .data3 = fwd };
            D_PostEvent(&ev);
        }
#if LEFT_STICK_MOVE
        if (ax[0] < -STICK_STRAFE) vk |= VK_LS_LEFT; else if (ax[0] > STICK_STRAFE) vk |= VK_LS_RIGHT;
#endif
    } else {
#if LEFT_STICK_MOVE
        if (ax[1] < -STICK_DEAD) vk |= VK_LS_UP;   else if (ax[1] > STICK_DEAD) vk |= VK_LS_DOWN;
        if (ax[0] < -STICK_DEAD) vk |= VK_LS_MLEFT; else if (ax[0] > STICK_DEAD) vk |= VK_LS_MRIGHT;
#endif
    }
    // diagnostics: axes and report rate at most 4x a second while any stick or trigger is active
    static int64_t last_log; static uint32_t last_reports;
    int64_t now = esp_timer_get_time();
    if (now - last_log > 1000000 && (abs(ax[2]) > STICK_DEAD || (vk & VK_TRIGGER))) {
        uint32_t r = ble_pad_reports();
        printf("axes L %6d %6d R %6d %6d T %5d %5d level %d, %lu reports in %lld ms\n", ax[0], ax[1], ax[2], ax[3], ax[4], ax[5], in_level,
               (unsigned long)(r - last_reports), (long long)((now - last_log) / 1000));
        last_log = now; last_reports = r;
    }
    return vk;
}

// Medal mode: after doom_idle_sleep_s without input while the attract loop runs, put the panel to
// sleep and park the game task (tics stop, the DAC ring drains to silence, BLE keeps listening).
// Any medal button or pad button wakes it. 0 disables.
#ifndef DOOM_IDLE_SLEEP_S
#define DOOM_IDLE_SLEEP_S 0      // 0 = never: the medal stays visual in attract (Jesse); NVS medal/idle_s can turn it on
#endif
int doom_idle_sleep_s = DOOM_IDLE_SLEEP_S;   // build default; NVS medal/idle_s overrides
static int64_t last_input_us;

static void idle_sleep_if_due(uint32_t vk)
{
    int64_t now = esp_timer_get_time();
    if (vk) last_input_us = now;
    if (!doom_idle_sleep_s || !demoplayback || now - last_input_us < (int64_t)doom_idle_sleep_s * 1000000) return;
    printf("idle %d s in attract: display sleep\n", doom_idle_sleep_s);
    vTaskDelay(pdMS_TO_TICKS(150));          // let the display task finish the frame in flight
    display_sleep(true);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(PIN_BTN_BOOT) == 0 || gpio_get_level(PIN_BTN_PWR) == 0 || ble_pad_buttons()) break;
    }
    display_sleep(false);
    last_input_us = esp_timer_get_time();
    printf("wake\n");
}

void I_GetEvent(void)
{
    uint32_t serial = serial_vkeys();
    medal_vkeys(serial & VK_MEDAL_BOOT);                       // device-only gestures, nothing posted
    uint32_t vk = ble_pad_buttons() | stick_vkeys() | (serial & ~(VK_MEDAL_BOOT | VK_MEDAL_PWR_TAP | VK_MEDAL_PWR_HOLD));
    idle_sleep_if_due(vk);
    if (gamestate == GS_LEVEL && !menuactive && !demoplayback && (vk & (PAD_LEFT | PAD_RIGHT))) {
        // d-pad left/right strafe in a level; the right stick turns. In menus they stay arrows.
        if (vk & PAD_LEFT) vk |= VK_LS_LEFT;
        if (vk & PAD_RIGHT) vk |= VK_LS_RIGHT;
        vk &= ~(PAD_LEFT | PAD_RIGHT);
    }
    {   // settings, battery, Konami code: fed the raw pad presses, not the remapped keys
        static uint32_t prev_pad;
        uint32_t pad = ble_pad_buttons() | (serial & 0xffff) | (serial & VK_MEDAL_PWR_TAP);   // bench 'n' = PWR tap
        if (pad & ~prev_pad & VK_MEDAL_PWR_TAP) extras_battery_toast();
        extras_poll(pad & ~prev_pad, gamestate == GS_LEVEL && !menuactive && !demoplayback);
        prev_pad = pad;
    }
    uint32_t down = vk & ~prev_vk, up = prev_vk & ~vk;
    if (down) { post_set(down, ev_keydown); printf("input: down 0x%05lx\n", (unsigned long)down); }
    if (up) post_set(up, ev_keyup);
    prev_vk = vk;

    bool connected = ble_pad_state() == PAD_CONNECTED;
    // Saved-pad-only scanning gets the bonded pad back quickly, but a pad put into pairing mode
    // comes back with a new address: after 10 s without a link, take any HID gamepad (NESTOR's
    // controller screen did the same after 5 s).
    static bool open_scan;
    if (!connected && !open_scan && esp_timer_get_time() > 10000000) { open_scan = true; ble_pad_scan_any(true); printf("gamepad: pairing open\n"); }
    if (connected && open_scan) open_scan = false;
    if (connected != pad_was_connected) {
        pad_was_connected = connected;
        printf("gamepad %s: %s\n", connected ? "connected" : "disconnected", ble_pad_name());
        if (connected) ble_pad_scan_any(false);     // reconnects go to this pad only
    }
}

void I_GetEventTimeout(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
    I_GetEvent();
}

void I_StartTextInput(int x1, int y1, int x2, int y2) {}
void I_StopTextInput(void) {}
void I_ReadMouse(void) {}
void I_BindInputVariables(void) {}
int GetTypedChar(int scancode, boolean shiftdown) { return 0; }
