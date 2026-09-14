/*
 * ble_pad.h - one BLE HID gamepad, via NimBLE and ESP-IDF's esp_hidh.
 *
 * Boot: if a controller was saved in NVS we scan for that address only and
 * reconnect when it advertises. ble_pad_scan_any(true) (the sync screen)
 * accepts the first HID gamepad seen instead, and saves it on connect.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAD_UP      0x001
#define PAD_DOWN    0x002
#define PAD_LEFT    0x004
#define PAD_RIGHT   0x008
#define PAD_A       0x010
#define PAD_B       0x020
#define PAD_START   0x040
#define PAD_SELECT  0x080
#define PAD_MENU    0x100   /* Y or a shoulder button (kept for callers that only want "some other button") */
#define PAD_X       0x200
#define PAD_Y       0x400
#define PAD_L       0x800
#define PAD_R       0x1000

typedef enum { PAD_IDLE, PAD_SCANNING, PAD_CONNECTING, PAD_CONNECTED } ble_pad_state_t;

void ble_pad_init(void);                 /* nvs_flash_init() must have run */
void ble_pad_scan_any(bool any);         /* true: pair with any gamepad; false: saved one only */
void ble_pad_scan_rate(bool fast);       /* fast: 60 % receive duty (pairing screen); slow: 3 % (unattended, saves ~30 mA) */
ble_pad_state_t ble_pad_state(void);
const char *ble_pad_name(void);          /* controller found / connected, "" if none */
bool ble_pad_has_saved(void);
void ble_pad_forget(void);               /* drop the saved controller and its bond */
uint32_t ble_pad_buttons(void);          /* PAD_* mask */
void ble_pad_axes(int16_t axes[4]);      /* LX, LY, RX, RY centred (-32768..32767; up/left negative); 0 when the pad has no such axis */
uint32_t ble_pad_raw(void);              /* raw HID button bits (bit n = button n+1), for the serial log */
uint32_t ble_pad_adv_seen(void);         /* advertisements seen since boot: proves the scanner is alive */

#ifdef __cplusplus
}
#endif
