// Input for the ESP32-C6 build: BLE gamepad (NESTOR's ble_pad) plus the medal's two buttons,
// both turned into Doom key events. Doom only ever sees keys, so a pad button is a set of key
// codes: the set covers the in-game meaning and the menu meaning at once (harmless overlap).
//
// Medal buttons (no controller nearby):
//   BOOT held      = fire (in a menu / on the title: also opens the menu / picks "down")
//   PWR tap        = use / menu forward (Enter)
//   PWR 0.6 s hold = menu (Escape); 3 s = power off (medal.c)
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
#include "esp_vfs_usb_serial_jtag.h"

float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int novert = 0;

#define PIN_BTN_BOOT GPIO_NUM_9
#define PIN_BTN_PWR  GPIO_NUM_18
#define PWR_MENU_HOLD_US 600000

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
    { VK_MEDAL_BOOT,     { KEY_RCTRL, KEY_DOWNARROW } },
    { VK_MEDAL_PWR_TAP,  { ' ', KEY_ENTER, 'y' } },
    { VK_MEDAL_PWR_HOLD, { KEY_ESCAPE } },
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

// PWR is edge-classified on release (tap) or the moment the hold threshold passes (hold), each
// as a one-frame virtual key press so Doom sees a clean down/up pair.
static uint32_t medal_vkeys(void)
{
    static int64_t pwr_down_since;
    static bool pwr_hold_fired, armed[2];
    static uint32_t pulse;                    // one-shot bits to release next call
    uint32_t vk = 0;
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0, pwr = gpio_get_level(PIN_BTN_PWR) == 0;
    // a button still held from power-on must be released once before it counts (medal.c does the same)
    if (!armed[0]) { if (!boot) armed[0] = true; boot = false; }
    if (!armed[1]) { if (!pwr) armed[1] = true; pwr = false; }
    if (boot) vk |= VK_MEDAL_BOOT;
    // BOOT held 10 s: forget the paired controller and open pairing (NESTOR's escape hatch)
    static int64_t boot_down_since; static bool boot_forgot;
    if (boot && !boot_down_since) { boot_down_since = now; boot_forgot = false; }
    if (!boot) boot_down_since = 0;
    if (boot && !boot_forgot && now - boot_down_since >= 10000000) {
        boot_forgot = true;
        ble_pad_forget(); ble_pad_scan_any(true);
        printf("gamepad: forgotten, pairing open\n");
    }
    if (pulse) { pulse = 0; }                 // pulse bits were down for exactly one poll
    if (pwr && !pwr_down_since) { pwr_down_since = now; pwr_hold_fired = false; }
    if (pwr && !pwr_hold_fired && now - pwr_down_since >= PWR_MENU_HOLD_US) { pwr_hold_fired = true; pulse |= VK_MEDAL_PWR_HOLD; }
    if (!pwr && pwr_down_since) {
        if (!pwr_hold_fired && now - pwr_down_since > 30000) pulse |= VK_MEDAL_PWR_TAP;
        pwr_down_since = 0;
    }
    return vk | pulse;
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
        int fwd = -axis_scaled(ax[1], STICK_DEAD, MOVE_MAX, false);    // HID Y grows downward
        if (turn || fwd) {
            event_t ev = { .type = ev_mouse, .data1 = 0, .data2 = turn, .data3 = fwd };
            D_PostEvent(&ev);
        }
        if (ax[0] < -STICK_STRAFE) vk |= VK_LS_LEFT; else if (ax[0] > STICK_STRAFE) vk |= VK_LS_RIGHT;
    } else {
        if (ax[1] < -STICK_DEAD) vk |= VK_LS_UP;   else if (ax[1] > STICK_DEAD) vk |= VK_LS_DOWN;
        if (ax[0] < -STICK_DEAD) vk |= VK_LS_MLEFT; else if (ax[0] > STICK_DEAD) vk |= VK_LS_MRIGHT;
    }
    // diagnostics: axes and report rate at most 4x a second while any stick or trigger is active
    static int64_t last_log; static uint32_t last_reports;
    int64_t now = esp_timer_get_time();
    if (now - last_log > 250000 && (abs(ax[0]) > STICK_DEAD || abs(ax[1]) > STICK_DEAD || abs(ax[2]) > STICK_DEAD || (vk & VK_TRIGGER))) {
        uint32_t r = ble_pad_reports();
        printf("axes L %6d %6d R %6d %6d T %5d %5d level %d, %lu reports in %lld ms\n", ax[0], ax[1], ax[2], ax[3], ax[4], ax[5], in_level,
               (unsigned long)(r - last_reports), (long long)((now - last_log) / 1000));
        last_log = now; last_reports = r;
    }
    return vk;
}

void I_GetEvent(void)
{
    uint32_t vk = ble_pad_buttons() | stick_vkeys() | medal_vkeys() | serial_vkeys();
    if (gamestate == GS_LEVEL && !menuactive && !demoplayback && (vk & (PAD_LEFT | PAD_RIGHT))) {
        // d-pad left/right strafe in a level; the right stick turns. In menus they stay arrows.
        if (vk & PAD_LEFT) vk |= VK_LS_LEFT;
        if (vk & PAD_RIGHT) vk |= VK_LS_RIGHT;
        vk &= ~(PAD_LEFT | PAD_RIGHT);
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
