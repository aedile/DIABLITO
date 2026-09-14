#include "ble_pad.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "esp_hidh.h"

static const char *TAG = "PAD";

#define NVS_NS  "diablito"
#define NVS_KEY "pad"

/* ---- HID report descriptor: where the buttons, hat and sticks live ---- */
typedef struct {
    uint8_t rid, size;
    uint16_t bit;
    uint16_t page, usage;
    int32_t lmin, lmax;
} hid_field_t;

static hid_field_t fields[64];
static int nfields;

static int32_t sign_ext(uint32_t v, int sz)
{
    if (sz == 1) return (int8_t)v;
    if (sz == 2) return (int16_t)v;
    return (int32_t)v;
}

static void hid_parse(const uint8_t *d, size_t len)
{
    uint16_t page = 0; int32_t lmin = 0, lmax = 0;
    uint32_t rsize = 0, rcount = 0, rid = 0;
    uint16_t usages[32]; int nus = 0; uint32_t umin = 0;
    struct { uint8_t rid; uint16_t bits; } pos[8] = {0}; int npos = 0;
    nfields = 0;

    for (size_t i = 0; i < len;) {
        uint8_t p = d[i++];
        if (p == 0xFE) { if (i < len) i += 2 + d[i]; continue; }   /* long item */
        int sz = p & 3; if (sz == 3) sz = 4;
        uint32_t v = 0;
        for (int k = 0; k < sz && i + k < len; k++) v |= (uint32_t)d[i + k] << (8 * k);
        i += sz;
        int type = (p >> 2) & 3, tag = p >> 4;
        if (type == 1) {              /* global */
            switch (tag) {
            case 0: page = v; break;
            case 1: lmin = sign_ext(v, sz); break;
            case 2: lmax = sign_ext(v, sz); break;
            case 7: rsize = v; break;
            case 8: rid = v; break;
            case 9: rcount = v; break;
            }
        } else if (type == 2) {       /* local */
            switch (tag) {
            case 0: if (nus < 32) usages[nus++] = v & 0xFFFF; break;
            case 1: umin = v; break;
            case 2: for (uint32_t u = umin; u <= v && nus < 32; u++) usages[nus++] = u; break;
            }
        } else if (type == 0) {       /* main */
            if (tag == 8) {           /* Input */
                int pi = 0;
                for (; pi < npos; pi++) if (pos[pi].rid == rid) break;
                if (pi == npos && npos < 8) pos[npos++].rid = rid;
                for (uint32_t k = 0; k < rcount && pi < 8; k++) {
                    uint16_t u = nus ? usages[k < (uint32_t)nus ? k : nus - 1] : 0;
                    if (!(v & 1) && nfields < 64 && (page == 9 || page == 1 || page == 7 || page == 2))
                        fields[nfields++] = (hid_field_t){ rid, rsize, pos[pi].bits, page, u, lmin, lmax };
                    pos[pi].bits += rsize;
                }
            }
            nus = 0; umin = 0;
        }
    }
    ESP_LOGI(TAG, "report map: %d input fields", nfields);
    for (int i = 0; i < nfields; i++)
        ESP_LOGI(TAG, "  field rid %u page %u usage 0x%02x bits %u @%u logical %ld..%ld", fields[i].rid, fields[i].page,
                 fields[i].usage, fields[i].size, fields[i].bit, (long)fields[i].lmin, (long)fields[i].lmax);
    for (int f = 0; f < nfields; f++)
        ESP_LOGI(TAG, "  rid %u page %x usage %x bit %u size %u [%ld..%ld]", fields[f].rid, fields[f].page,
                 fields[f].usage, fields[f].bit, fields[f].size, (long)fields[f].lmin, (long)fields[f].lmax);
}

static uint32_t get_bits(const uint8_t *d, size_t len, int bit, int size)
{
    uint32_t v = 0;
    for (int i = 0; i < size && i < 32; i++) {
        int b = bit + i;
        if ((size_t)(b >> 3) < len) v |= (uint32_t)((d[b >> 3] >> (b & 7)) & 1) << i;
    }
    return v;
}

/* Default map is the Xbox Wireless Controller's BLE button numbering:
 * 1 A, 2 B, 4 X, 5 Y, 7 LB, 8 RB, 11 View/Select, 12 Menu/Start.
 * Positions, not labels: the NES has A on the right and B below, which is Xbox B and A.
 * NOTE: 8BitDo pads (Micro, Zero 2, SN30...), PS4/PS5 and Switch controllers are Bluetooth
 * Classic and can never connect to this board; the C6 has no Classic radio. */
#define BTN(n) (1u << ((n) - 1))
static uint32_t map_buttons(uint32_t raw)
{
    uint32_t b = 0;
    if (raw & BTN(1)) b |= PAD_A;          /* Xbox A: labels, not positions, for Doom */
    if (raw & BTN(2)) b |= PAD_B;
    if (raw & BTN(4)) b |= PAD_X;
    if (raw & BTN(5)) b |= PAD_Y;
    if (raw & BTN(7)) b |= PAD_L;
    if (raw & BTN(8)) b |= PAD_R;
    if (raw & BTN(12)) b |= PAD_START;
    if (raw & BTN(11)) b |= PAD_SELECT;
    if (raw & (BTN(5) | BTN(7) | BTN(8))) b |= PAD_MENU;
    return b;
}

static volatile uint32_t cur_buttons, cur_raw;
static volatile int16_t cur_axes[6];      /* LX LY RX RY centred, LT RT 0..32767 */
static volatile uint32_t report_count;

void ble_pad_axes(int16_t axes[6]) { for (int i = 0; i < 6; i++) axes[i] = cur_axes[i]; }
uint32_t ble_pad_reports(void) { return report_count; }

static void decode_report(uint8_t rid, const uint8_t *d, size_t len)
{
    uint32_t raw = 0, dir = 0;
    for (int f = 0; f < nfields; f++) {
        const hid_field_t *h = &fields[f];
        if (h->rid != rid) continue;
        uint32_t v = get_bits(d, len, h->bit, h->size);
        if (h->page == 9) {
            if (v && h->usage >= 1 && h->usage <= 32) raw |= 1u << (h->usage - 1);
        } else if (h->page == 2) {           /* simulation controls: 0xC5 brake = LT, 0xC4 accelerator = RT */
            int32_t range = h->lmax - h->lmin;
            if (range > 0 && (h->usage == 0xC5 || h->usage == 0xC4))
                cur_axes[h->usage == 0xC5 ? 4 : 5] = (int16_t)((int64_t)((int32_t)v - h->lmin) * 32767 / range);
        } else if (h->page == 1) {
            int32_t sv = h->lmin < 0 ? sign_ext(v, (h->size + 7) / 8) : (int32_t)v;
            int32_t range = h->lmax - h->lmin;
            switch (h->usage) {
            case 0x39: {                       /* hat: 8 positions clockwise from up */
                int32_t p = sv - h->lmin;
                if (p >= 0 && p <= 7) {
                    if (p == 7 || p == 0 || p == 1) dir |= PAD_UP;
                    if (p >= 1 && p <= 3) dir |= PAD_RIGHT;
                    if (p >= 3 && p <= 5) dir |= PAD_DOWN;
                    if (p >= 5 && p <= 7) dir |= PAD_LEFT;
                }
                break;
            }
            /* sticks: X/Y left, Z/Rz right on Xbox and most pads; handed to the game as analog axes */
            case 0x30: case 0x31: case 0x32: case 0x35:
                if (range > 0) {
                    int ax = h->usage == 0x30 ? 0 : h->usage == 0x31 ? 1 : h->usage == 0x32 ? 2 : 3;
                    cur_axes[ax] = (int16_t)((int64_t)(sv - h->lmin) * 65535 / range - 32768);
                }
                break;
            case 0x90: if (v) dir |= PAD_UP; break;     /* DPad usages, some pads */
            case 0x91: if (v) dir |= PAD_DOWN; break;
            case 0x92: if (v) dir |= PAD_RIGHT; break;
            case 0x93: if (v) dir |= PAD_LEFT; break;
            }
        }
    }
    /* keyboard-mode pads (8BitDo Micro / Zero 2 in K mode) send HID keycodes in 8-bit array
     * slots on page 7; 8BitDo's layout is letters C..O */
    for (int f = 0; f < nfields; f++) {
        const hid_field_t *h = &fields[f];
        if (h->rid != rid || h->page != 7 || h->size != 8) continue;
        uint32_t key = get_bits(d, len, h->bit, 8);
        switch (key) {
        case 0x06: dir |= PAD_UP; break;      /* C */
        case 0x07: dir |= PAD_DOWN; break;    /* D */
        case 0x08: dir |= PAD_LEFT; break;    /* E */
        case 0x09: dir |= PAD_RIGHT; break;   /* F */
        case 0x0A: dir |= PAD_A; break;       /* G: A, the right-hand button */
        case 0x0D: dir |= PAD_B; break;       /* J: B, the lower button */
        case 0x0B: dir |= PAD_B; break;       /* H: X, doubles as B */
        case 0x0C: case 0x0E: case 0x10: dir |= PAD_MENU; break;   /* I: Y, K: L, M: R */
        case 0x11: dir |= PAD_SELECT; break;  /* N */
        case 0x12: dir |= PAD_START; break;   /* O */
        default: if (key) raw |= 0x10000 | (key << 20); break;   /* unknown key: visible in the raw log */
        }
    }
    cur_raw = raw;
    cur_buttons = map_buttons(raw & 0xFFFF) | dir;
    report_count++;
}

/* ---- connection state ---- */
static struct { uint8_t addr[6]; uint8_t type; } saved, target;
static uint8_t bad_addr[6];
static int64_t bad_until;
static bool have_saved, accept_any;
static volatile ble_pad_state_t state = PAD_IDLE;
static char found_name[32];
static esp_hidh_dev_t *dev;
static QueueHandle_t cmdq;
enum { CMD_SCAN, CMD_CONNECT };

static void nvs_load(void)
{
    nvs_handle_t h;
    have_saved = false;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof saved;
    have_saved = nvs_get_blob(h, NVS_KEY, &saved, &len) == ESP_OK && len == sizeof saved;
    nvs_close(h);
}

static void nvs_store(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, &saved, sizeof saved);
    nvs_commit(h);
    nvs_close(h);
}

static void post(int cmd) { xQueueSend(cmdq, &cmd, 0); }

static volatile uint32_t adv_seen;

static int gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *disc = &event->disc;
        struct ble_hs_adv_fields f;
        adv_seen++;
        if (ble_hs_adv_parse_fields(&f, disc->data, disc->length_data) != 0) return 0;
        bool is_hid = (f.appearance_is_present && (f.appearance >> 6) == 0x0F);
        for (int i = 0; i < f.num_uuids16 && !is_hid; i++)
            if (ble_uuid_u16(&f.uuids16[i].u) == 0x1812) is_hid = true;
        bool is_saved = have_saved && memcmp(disc->addr.val, saved.addr, 6) == 0;
        /* Some pads (8BitDo Micro in D mode) advertise neither an HID appearance nor the HID
         * service. While pairing is open, a named device held right against the medal is taken
         * anyway; the HID discovery after connecting is what decides if it is a controller. */
        bool held_close = f.name_len && disc->rssi >= -45;
        if (f.name_len || f.appearance_is_present)
            ESP_LOGD(TAG, "seen %.*s appearance %04x uuids16 %d uuids128 %d hid %d rssi %d", f.name_len, (const char *)f.name,
                     f.appearance_is_present ? f.appearance : 0, f.num_uuids16, f.num_uuids128, is_hid, disc->rssi);
        if (!(is_saved || (accept_any && (is_hid || held_close)))) return 0;
        if (esp_timer_get_time() < bad_until && memcmp(disc->addr.val, bad_addr, 6) == 0) return 0;
        if (f.name_len) {
            int n = f.name_len < (int)sizeof found_name - 1 ? f.name_len : (int)sizeof found_name - 1;
            memcpy(found_name, f.name, n); found_name[n] = 0;
        }
        ESP_LOGI(TAG, "found %s %02x:%02x:%02x:%02x:%02x:%02x rssi %d", found_name, disc->addr.val[5],
                 disc->addr.val[4], disc->addr.val[3], disc->addr.val[2], disc->addr.val[1], disc->addr.val[0], disc->rssi);
        memcpy(target.addr, disc->addr.val, 6);
        target.type = disc->addr.type;
        ble_gap_disc_cancel();
        post(CMD_CONNECT);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        if (state == PAD_SCANNING) post(CMD_SCAN);   /* keep scanning until we connect */
    }
    return 0;
}

static bool scan_fast = true;

static void start_scan(void)
{
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;
    /* units of 0.625 ms. fast: 30 ms window every 50 ms. slow: 30 ms window every second, so a
     * pad switched on near an unattended medal still connects within a few seconds while the
     * receiver is off 97 % of the time */
    struct ble_gap_disc_params p = { .itvl = scan_fast ? 0x50 : 1600, .window = 0x30, .filter_duplicates = 1, .passive = 0 };
    int rc = ble_gap_disc(own_addr_type, 30000, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "ble_gap_disc rc=%d", rc);
    state = PAD_SCANNING;
}

static void hidh_callback(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_data_t *p = event_data;
    switch ((esp_hidh_event_t)id) {
    case ESP_HIDH_OPEN_EVENT:
        if (p->open.status != ESP_OK) {
            if (have_saved && memcmp(saved.addr, target.addr, 6) == 0) {
                /* the saved pad refused us: its bond is gone (re-paired elsewhere or put back into
                 * pairing mode). Drop ours and pair fresh on the next sighting, no penalty box. */
                ESP_LOGW(TAG, "open failed: %d on the saved pad, forgetting it and pairing fresh", p->open.status);
                ble_pad_forget();
                accept_any = true;
            } else {
                ESP_LOGW(TAG, "open failed: %d, ignoring that device for a minute", p->open.status);
                memcpy(bad_addr, target.addr, 6);
                bad_until = esp_timer_get_time() + 60000000;
            }
            dev = NULL;
            post(CMD_SCAN);
            break;
        }
        dev = p->open.dev;
        {
            const char *n = esp_hidh_dev_name_get(dev);
            if (n && *n) strlcpy(found_name, n, sizeof found_name);
            size_t nmaps = 0; esp_hid_raw_report_map_t *maps = NULL;
            if (esp_hidh_dev_report_maps_get(dev, &nmaps, &maps) == ESP_OK && nmaps && maps[0].len)
                hid_parse(maps[0].data, maps[0].len);
            else
                ESP_LOGW(TAG, "no report map");   /* the host rejects such devices before OPEN now */
        }
        memcpy(saved.addr, target.addr, 6); saved.type = target.type; have_saved = true;
        nvs_store();
        cur_buttons = cur_raw = 0;
        state = PAD_CONNECTED;
        ESP_LOGI(TAG, "connected: %s", found_name);
        break;
    case ESP_HIDH_INPUT_EVENT:
        decode_report(p->input.report_id, p->input.data, p->input.length);
        break;
    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGI(TAG, "disconnected (%d)", p->close.reason);
        esp_hidh_dev_free(p->close.dev);
        dev = NULL;
        cur_buttons = cur_raw = 0;
        post(CMD_SCAN);
        break;
    default:
        break;
    }
}

static void pad_task(void *arg)
{
    int cmd;
    for (;;) {
        if (xQueueReceive(cmdq, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd == CMD_SCAN) {
            if (state != PAD_CONNECTED && (have_saved || accept_any)) start_scan();
            else state = PAD_IDLE;
        } else if (cmd == CMD_CONNECT) {
            state = PAD_CONNECTING;
            /* blocks until the HID services are discovered; OPEN_EVENT reports the outcome */
            esp_hidh_dev_open(target.addr, ESP_HID_TRANSPORT_BLE, target.type);
        }
    }
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void) { post(CMD_SCAN); }
void ble_store_config_init(void);

void ble_pad_init(void)
{
    nvs_load();
    cmdq = xQueueCreate(4, sizeof(int));
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_our_key_dist = ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    esp_hidh_config_t cfg = { .callback = hidh_callback, .event_stack_size = 4096 };
    ESP_ERROR_CHECK(esp_hidh_init(&cfg));
    ble_store_config_init();
    xTaskCreate(pad_task, "pad", 4096, NULL, 5, NULL);
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "init, saved controller: %s", have_saved ? "yes" : "no");
}

void ble_pad_scan_any(bool any)
{
    accept_any = any;
    if (state == PAD_IDLE) post(CMD_SCAN);
}

void ble_pad_scan_rate(bool fast)
{
    if (scan_fast == fast) return;
    scan_fast = fast;
    ESP_LOGI(TAG, "scan rate: %s", fast ? "fast" : "slow");
    if (state == PAD_SCANNING) { ble_gap_disc_cancel(); post(CMD_SCAN); }   /* restart with the new parameters */
}

ble_pad_state_t ble_pad_state(void) { return state; }
const char *ble_pad_name(void) { return found_name; }
bool ble_pad_has_saved(void) { return have_saved; }
uint32_t ble_pad_buttons(void) { return cur_buttons; }
uint32_t ble_pad_raw(void) { return cur_raw; }
uint32_t ble_pad_adv_seen(void) { return adv_seen; }

void ble_pad_forget(void)
{
    if (!have_saved) return;
    ble_addr_t a = { .type = saved.type };
    memcpy(a.val, saved.addr, 6);
    ble_gap_unpair(&a);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { nvs_erase_key(h, NVS_KEY); nvs_commit(h); nvs_close(h); }
    have_saved = false;
    found_name[0] = 0;
    if (dev) esp_hidh_dev_close(dev);
    ESP_LOGI(TAG, "forgot saved controller");
}
