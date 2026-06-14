// buzzer.c — Buzzer via LEDC (eigener Timer, getrennt vom Dimmer)
#include "buzzer.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#define BZ_TIMER    LEDC_TIMER_1
#define BZ_CH       LEDC_CHANNEL_2
#define BZ_MODE     LEDC_LOW_SPEED_MODE

esp_err_t buzzer_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = BZ_MODE, .timer_num = BZ_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = BUZZER_PIN, .speed_mode = BZ_MODE, .channel = BZ_CH,
        .timer_sel = BZ_TIMER, .duty = 0, .hpoint = 0,
    };
    ledc_channel_config(&c);
    ESP_LOGI("buzzer", "Buzzer init (GPIO%d)", BUZZER_PIN);
    return ESP_OK;
}

void buzzer_beep(int freq_hz, int ms)
{
    ledc_set_freq(BZ_MODE, BZ_TIMER, freq_hz);
    ledc_set_duty(BZ_MODE, BZ_CH, 512);   // 50 % bei 10-bit
    ledc_update_duty(BZ_MODE, BZ_CH);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(BZ_MODE, BZ_CH, 0);
    ledc_update_duty(BZ_MODE, BZ_CH);
}

void buzzer_alarm(void)
{
    for (int i = 0; i < 3; i++) { buzzer_beep(2500, 150); vTaskDelay(pdMS_TO_TICKS(120)); }
}

// ── Nicht-blockierender Dauerton (z.B. 15s Lichtaus-Vorwarnung) ──
static esp_timer_handle_t s_off_timer = NULL;

static void tone_off_cb(void *arg)
{
    ledc_set_duty(BZ_MODE, BZ_CH, 0);
    ledc_update_duty(BZ_MODE, BZ_CH);
}

void buzzer_tone_ms(int freq_hz, int ms)
{
    if (!s_off_timer) {
        const esp_timer_create_args_t a = { .callback = tone_off_cb, .name = "bz_off" };
        esp_timer_create(&a, &s_off_timer);
    }
    esp_timer_stop(s_off_timer);              // evtl. laufenden Stopp abbrechen
    ledc_set_freq(BZ_MODE, BZ_TIMER, freq_hz);
    ledc_set_duty(BZ_MODE, BZ_CH, 512);
    ledc_update_duty(BZ_MODE, BZ_CH);
    esp_timer_start_once(s_off_timer, (uint64_t)ms * 1000);   // Auto-Aus nach ms
}
