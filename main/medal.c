#include "medal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"

static const char *TAG = "MEDAL";

#define PIN_BAT_EN   GPIO_NUM_15   /* battery rail enable: hold high or the medal browns out */
#define PIN_BTN_BOOT GPIO_NUM_9    /* active low, external pull-up */
#define PIN_BTN_PWR  GPIO_NUM_18
#define BAT_ADC_CH   ADC_CHANNEL_0 /* GPIO0, through the board's divider */

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;

void medal_init(void)
{
    gpio_config_t bat = { .pin_bit_mask = 1ULL << PIN_BAT_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t btn = { .pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR),
                          .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&btn);

    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&u, &adc) == ESP_OK) {
        adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        adc_oneshot_config_channel(adc, BAT_ADC_CH, &c);
        adc_cali_curve_fitting_config_t cc = { .unit_id = ADC_UNIT_1, .chan = BAT_ADC_CH, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        if (adc_cali_create_scheme_curve_fitting(&cc, &cali) != ESP_OK) cali = NULL;
    }
}

/* One event per press. PWR: SHORT on release under 2 s, LONG at 3 s. BOOT: SHORT on release
 * under 3 s, HOLD3 the moment 3 s is reached, HOLD10 the moment 10 s is reached. */
uint32_t medal_poll(void)
{
    static int64_t down_since[2];
    static bool fired3, fired[2], armed[2];
    static const gpio_num_t pin[2] = { PIN_BTN_BOOT, PIN_BTN_PWR };
    static const int64_t hold_us[2] = { 10000000, 3000000 };
    int64_t now = esp_timer_get_time();
    uint32_t ev = 0;
    for (int i = 0; i < 2; i++) {
        bool down = gpio_get_level(pin[i]) == 0;
        /* the medal is switched on by holding PWR: a button still held from before boot must
         * be released once before it counts, or the power-off hold fires and reboots the medal */
        if (!armed[i]) { if (!down) armed[i] = true; continue; }
        if (down && !down_since[i]) { down_since[i] = now; fired[i] = false; fired3 = false; }
        if (down && i == 0 && !fired3 && now - down_since[i] >= 3000000) { fired3 = true; ev |= BTN_BOOT_HOLD3; }
        if (down && !fired[i] && now - down_since[i] >= hold_us[i]) { fired[i] = true; ev |= i ? BTN_PWR_LONG : BTN_BOOT_HOLD10; }
        if (!down && down_since[i]) {
            int64_t held = now - down_since[i];
            if (!fired[i] && held > 30000 && held < 3000000) ev |= i ? BTN_PWR_SHORT : BTN_BOOT_SHORT;   /* 30 ms debounce */
            down_since[i] = 0;
        }
    }
    if (ev & BTN_PWR_LONG) medal_power_off();
    return ev;
}

static int last_mv;

int medal_battery_percent(void)
{
    static int64_t last;
    static int pct = -1;
    int64_t now = esp_timer_get_time();
    if (adc && (pct < 0 || now - last > 5000000)) {
        last = now;
        int raw = 0, mv = 0;
        if (adc_oneshot_read(adc, BAT_ADC_CH, &raw) == ESP_OK) {
            if (!cali || adc_cali_raw_to_voltage(cali, raw, &mv) != ESP_OK) mv = raw * 3300 / 4095;
            /* ponytail: divider factor 3 is what Waveshare's own battery example uses for this board;
             * tune here if the reading is off, and the 3.3-4.2 V linear map is a guess at a curve */
            last_mv = mv * 3;
            int p = (last_mv - 3300) * 100 / (4200 - 3300);
            pct = p < 0 ? 0 : p > 100 ? 100 : p;
        }
    }
    return pct < 0 ? 0 : pct;
}

int medal_battery_mv(void) { medal_battery_percent(); return last_mv; }

void medal_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    display_set_backlight(0);
    gpio_set_level(PIN_BAT_EN, 0);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));   /* on USB the rail stays up: sit dark until reset */
}
