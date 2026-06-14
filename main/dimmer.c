// dimmer.c — 0-10V Dimmer via LEDC-PWM
#include "dimmer.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_log.h"
#include "nvsutil.h"

static const char *TAG = "dimmer";

// PWM-Frequenz: ~2 kHz reicht; der RC-Filter vor dem OpAmp glättet zu DC.
// 12-bit Auflösung (0..4095).
#define DIM_TIMER       LEDC_TIMER_0
#define DIM_MODE        LEDC_LOW_SPEED_MODE
// 20 kHz: bei 2 kHz erreichte der RC/OpAmp die Extreme (ganz aus) nicht sauber — die Lampe
// liess sich nicht voll runterregeln. Höhere Frequenz → bessere Glättung. 10-bit reicht für 0-100%.
#define DIM_FREQ_HZ     20000
#define DIM_RES         LEDC_TIMER_10_BIT
#define DIM_DUTY_MAX    1023

static const struct { gpio_num_t gpio; ledc_channel_t ch; } k_ch[DIM_COUNT] = {
    [DIM_LIGHT1] = { DIM1_PWM_PIN, LEDC_CHANNEL_0 },
    [DIM_LIGHT2] = { DIM2_PWM_PIN, LEDC_CHANNEL_1 },
};
static uint8_t s_pct[DIM_COUNT];
static bool    s_invert[DIM_COUNT];
// Nullpunkt-Kalibrierung je Kanal (Ausgang in % bei Slider 0), optional konfigurierbar.
// Default 0: die leistungs-lineare 10-Stufen-Kurve (k_step_pct, Stufe0=0) bildet den nutzbaren
// Bereich bereits ab — ein zusätzlicher s_min>0 würde doppelt kompensieren (am Gerät verifiziert).
static uint8_t s_min[DIM_COUNT] = { 0, 0 };

// 10-Stufen-Kalibrierung (leistungs-linear, am Gerät per BL0940 gemessen 2026-06-05).
// Index 0 = minimal/aus, 1..10 = gleichmäßige Watt-Schritte über den nutzbaren Bereich.
//   Licht1 (groß, 103..490 W): Min bei pct0, Max ab pct80.
//   Licht2 (klein, 60..144 W): Einschalt-Schwelle ~pct35; Stufe0 = aus (pct0).
static const uint8_t k_step_pct[DIM_COUNT][11] = {
    //              St0  1   2   3   4   5   6   7   8   9  10
    [DIM_LIGHT1] = {  0,  0,  9, 18, 26, 34, 43, 51, 59, 68, 80},
    [DIM_LIGHT2] = {  0, 40, 47, 53, 60, 66, 74, 80, 86, 94,100},
};

static void load_invert(void)
{
    uint8_t v = 0;
    if (nvsu_get_u8("dimmer", "inv0", &v)) s_invert[0] = v;
    if (nvsu_get_u8("dimmer", "inv1", &v)) s_invert[1] = v;
    if (nvsu_get_u8("dimmer", "min0", &v) && v < 100) s_min[0] = v;
    if (nvsu_get_u8("dimmer", "min1", &v) && v < 100) s_min[1] = v;
}

esp_err_t dimmer_init(void)
{
    load_invert();
    // WICHTIG: GPIO35/37 explizit als GPIO zurücksetzen, BEVOR LEDC sie übernimmt.
    // Bei diesen S3-Pins bleibt das IO-MUX sonst in einem Zustand, in dem das
    // LEDC-PWM-Signal NICHT am Pin ankommt (am Gerät verifiziert: ohne reset → 0V,
    // mit reset → 0-10V am Dimmer-OpAmp). gpio_reset_pin stellt die GPIO-Funktion her.
    // PWM-Pins (GPIO35/37) sauber resetten, bevor LEDC sie übernimmt.
    gpio_reset_pin(DIM1_PWM_PIN);
    gpio_reset_pin(DIM2_PWM_PIN);
    // ENABLE-Pins (GPIO38/36) der DIM-Stufe statisch aktivieren — ohne sie kommt das PWM
    // nicht durch (Lampe bleibt auf Default/Vollgas). Default HIGH = aktiv; per NVS umstellbar.
    gpio_config_t encfg = {
        .pin_bit_mask = (1ULL << DIM1_EN_PIN) | (1ULL << DIM2_EN_PIN),
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = 0, .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&encfg);
    gpio_set_level(DIM1_EN_PIN, 1);
    gpio_set_level(DIM2_EN_PIN, 1);

    ledc_timer_config_t tcfg = {
        .speed_mode      = DIM_MODE,
        .timer_num       = DIM_TIMER,
        .duty_resolution = DIM_RES,
        .freq_hz         = DIM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    for (int i = 0; i < DIM_COUNT; i++) {
        ledc_channel_config_t ccfg = {
            .gpio_num   = k_ch[i].gpio,
            .speed_mode = DIM_MODE,
            .channel    = k_ch[i].ch,
            .timer_sel  = DIM_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
        s_pct[i] = 0;
    }
    // Zuletzt manuell gesetzte Helligkeit wiederherstellen (überlebt Reboot). Für Lampen
    // einer geregelten Kammer überschreibt die Photoperiode den Wert ohnehin im 1. Zyklus;
    // bei Kammer „Aus" (kein Profil) bleibt der manuelle Wert erhalten.
    uint8_t pct[DIM_COUNT] = { 0, 10 };   // Default: Licht 1 = 0 %, Licht 2 = 10 %
    nvsu_load("dimmer", "pct", pct, sizeof(pct));   // gespeicherte Werte überschreiben die Defaults
    for (int i = 0; i < DIM_COUNT; i++) dimmer_set((dim_ch_t)i, pct[i]);
    ESP_LOGI(TAG, "Dimmer init (LIGHT1=GPIO%d, LIGHT2=GPIO%d)",
             DIM1_PWM_PIN, DIM2_PWM_PIN);
    return ESP_OK;
}

// Wie dimmer_set, aber persistiert den Wert in NVS (nur für MANUELLE Befehle aus
// WebUI/MQTT — NICHT aus der Regelschleife, sonst Flash-Wear durch 2-s-Takt).
// Persistiert nur bei echter Änderung; beide Kanäle als ein Blob (ein Commit).
esp_err_t dimmer_set_manual(dim_ch_t ch, uint8_t pct)
{
    if (ch >= DIM_COUNT) return ESP_ERR_INVALID_ARG;
    if (pct > 100) pct = 100;
    bool changed = (s_pct[ch] != pct);
    esp_err_t r = dimmer_set(ch, pct);
    if (changed) {
        uint8_t blob[DIM_COUNT];
        for (int i = 0; i < DIM_COUNT; i++) blob[i] = s_pct[i];
        nvsu_save("dimmer", "pct", blob, sizeof(blob));
    }
    return r;
}

esp_err_t dimmer_set(dim_ch_t ch, uint8_t pct)
{
    if (ch >= DIM_COUNT) return ESP_ERR_INVALID_ARG;
    if (pct > 100) pct = 100;
    // Nullpunkt-Kalibrierung: Slider-% → Ausgang s_min..100 % (Slider 0 = Aus-Kante der Lampe).
    uint32_t outp = (uint32_t)s_min[ch] + (uint32_t)pct * (100 - s_min[ch]) / 100;
    // Helligkeit % → PWM-Duty. Bei invertiertem Treiber (0V=hell): duty = max - x.
    uint32_t lvl = outp * DIM_DUTY_MAX / 100;
    uint32_t duty = s_invert[ch] ? (DIM_DUTY_MAX - lvl) : lvl;
    ESP_ERROR_CHECK(ledc_set_duty(DIM_MODE, k_ch[ch].ch, duty));
    ESP_ERROR_CHECK(ledc_update_duty(DIM_MODE, k_ch[ch].ch));
    s_pct[ch] = pct;
    ESP_LOGI(TAG, "LIGHT%d -> %u%% (duty %lu%s)", ch + 1, pct,
             (unsigned long)duty, s_invert[ch] ? ", inv" : "");
    return ESP_OK;
}

uint8_t dimmer_get(dim_ch_t ch)
{
    return (ch < DIM_COUNT) ? s_pct[ch] : 0;
}

void dimmer_set_invert(dim_ch_t ch, bool inv)
{
    if (ch >= DIM_COUNT) return;
    s_invert[ch] = inv;
    nvsu_set_u8("dimmer", ch == 0 ? "inv0" : "inv1", inv ? 1 : 0);
    dimmer_set(ch, s_pct[ch]);   // sofort mit neuer Polarität anwenden
    ESP_LOGI(TAG, "LIGHT%d Invertierung: %s", ch + 1, inv ? "AN" : "AUS");
}

bool dimmer_get_invert(dim_ch_t ch)
{
    return (ch < DIM_COUNT) ? s_invert[ch] : false;
}

// Nullpunkt setzen: Ausgang in % bei Slider 0 (z. B. 7 = Lampe geht hier aus). Persistiert.
void dimmer_set_cal(dim_ch_t ch, uint8_t min_pct)
{
    if (ch >= DIM_COUNT) return;
    if (min_pct > 99) min_pct = 99;
    s_min[ch] = min_pct;
    nvsu_set_u8("dimmer", ch == 0 ? "min0" : "min1", min_pct);
    dimmer_set(ch, s_pct[ch]);   // sofort mit neuem Nullpunkt anwenden
    ESP_LOGI(TAG, "LIGHT%d Nullpunkt → %u%%", ch + 1, min_pct);
}

uint8_t dimmer_get_cal(dim_ch_t ch)
{
    return (ch < DIM_COUNT) ? s_min[ch] : 0;
}

// 10-Stufen-Slider: Stufe 0..10 → leistungs-kalibrierter PWM-% (Lookup k_step_pct).
esp_err_t dimmer_set_step(dim_ch_t ch, uint8_t step)
{
    if (ch >= DIM_COUNT) return ESP_ERR_INVALID_ARG;
    if (step > 10) step = 10;
    ESP_LOGI(TAG, "LIGHT%d Stufe %u -> %u%%", ch + 1, step, k_step_pct[ch][step]);
    return dimmer_set_manual(ch, k_step_pct[ch][step]);
}

// Aktuelle Stufe aus dem gesetzten PWM-% rückrechnen (nächstliegende Stufe) — für die UI.
uint8_t dimmer_get_step(dim_ch_t ch)
{
    if (ch >= DIM_COUNT) return 0;
    uint8_t pct = s_pct[ch], best = 0; int bestdiff = 1000;
    for (int s = 0; s <= 10; s++) {
        int d = (int)pct - (int)k_step_pct[ch][s];
        if (d < 0) d = -d;
        if (d < bestdiff) { bestdiff = d; best = (uint8_t)s; }
    }
    return best;
}
