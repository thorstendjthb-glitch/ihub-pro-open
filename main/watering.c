// watering.c — siehe watering.h
#include "watering.h"
#include "relays.h"
#include "climate_control.h"
#include "nvsutil.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "watering";

static watering_cfg_t s_cfg = {
    .mode = 0, .only_day = 1, .duration_s = 60,
    .interval_h = 24, .moist_low = 40, .min_pause_min = 60,
};

static uint32_t s_last = 0;            // Epoch des letzten Gieß-Starts (NVS, überlebt Reboot)
static int64_t  s_run_until_us = 0;    // 0 = kein Vorgang aktiv; sonst monotones Ende
static int      s_rly = -1;            // Steckdose des laufenden Vorgangs

void watering_init(void)
{
    nvsu_load_partial("water", "cfg", &s_cfg, sizeof(s_cfg), NULL);
    nvsu_get_u32("water", "last", &s_last);
}

watering_cfg_t *watering_cfg(void) { return &s_cfg; }

void watering_save_cfg(void)
{
    nvsu_save("water", "cfg", &s_cfg, sizeof(s_cfg));
    ESP_LOGI(TAG, "Config gespeichert (Modus %u)", s_cfg.mode);
}

void watering_tick(const sensor_data_t *d, time_t now)
{
    // Laufenden Vorgang beenden (monotone Zeit — SNTP-Sprünge egal).
    if (s_run_until_us) {
        if (esp_timer_get_time() >= s_run_until_us) {
            s_run_until_us = 0;
            if (s_rly >= 0) relay_set_auto((relay_id_t)s_rly, false);
            ESP_LOGI(TAG, "Gießvorgang beendet");
        }
        return;                          // während des Gießens nichts neu starten
    }

    if (s_cfg.mode == 0) return;
    int rly = relay_role_find(FN_WATERING);
    if (rly < 0) return;                                   // keine Bewässerungs-Steckdose
    if (relay_get_mode((relay_id_t)rly) == RMODE_MANUAL) return;  // User-Override → Pause

    // Wanduhr nötig für Intervall/Pause — vor gültiger Zeit (SNTP/RTC) nicht gießen.
    struct tm tmv; localtime_r(&now, &tmv);
    if (tmv.tm_year < 124) return;                         // < 2024 = Zeit ungültig

    if (s_cfg.only_day) {
        int ch = relay_chamber((relay_id_t)rly);
        if (ch < 0) ch = 0;                                // keiner Kammer zugeordnet → A
        chamber_state_t cs; climate_chamber(ch, &cs);
        if (!cs.is_day) return;
    }

    bool due = false;
    if (s_cfg.mode == 1) {                                 // Intervall
        if (s_last == 0) {                                 // erstes Aktivieren: nur verankern,
            s_last = (uint32_t)now;                        // nicht sofort losgießen
            nvsu_set_u32("water", "last", s_last);
            return;
        }
        due = (now - (time_t)s_last) >= (time_t)s_cfg.interval_h * 3600;
    } else if (s_cfg.mode == 2) {                          // Substratfeuchte
        if (!d->substrate_valid) return;                   // ohne Sensor keine Blindbewässerung
        if (d->soil_moist_pct >= (float)s_cfg.moist_low) return;
        due = (s_last == 0) ||
              ((now - (time_t)s_last) >= (time_t)s_cfg.min_pause_min * 60);
    }
    if (!due) return;

    uint16_t dur = s_cfg.duration_s ? s_cfg.duration_s : 30;
    s_rly = rly;
    relay_set_auto((relay_id_t)rly, true);
    s_run_until_us = esp_timer_get_time() + (int64_t)dur * 1000000LL;
    s_last = (uint32_t)now;
    nvsu_set_u32("water", "last", s_last);                 // wenige Schreibzugriffe/Tag → kein Flash-Wear
    ESP_LOGI(TAG, "Gießvorgang gestartet (%u s, Modus %u, Substrat %.1f %%)",
             dur, s_cfg.mode, d->substrate_valid ? d->soil_moist_pct : -1.0f);
}

void watering_status(bool *active, uint32_t *last_start, uint32_t *next_due)
{
    if (active)     *active = (s_run_until_us != 0);
    if (last_start) *last_start = s_last;
    if (next_due)   *next_due = (s_cfg.mode == 1 && s_last)
                                ? s_last + (uint32_t)s_cfg.interval_h * 3600 : 0;
}
