// buttons.c — Taster mit Haltezeit-Logik + Buzzer-Feedback
#include "buttons.h"
#include "board_pins.h"
#include "relays.h"
#include "dimmer.h"
#include "buzzer.h"
#include "climate_control.h"
#include "wifi_conn.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define TICK_MS   100
#define T_POWER   50   // 5 s  (×100 ms)
#define T_BT_PROF 20   // 2 s
#define T_BT_RST  100  // 10 s

static void emergency_off(void)
{
    ESP_LOGW(TAG, "NOTAUS — alle Aktoren aus");
    for (int i = 0; i < RLY_MAX; i++) relay_set_manual(i, false);
    dimmer_set(DIM_LIGHT1, 0);
    dimmer_set(DIM_LIGHT2, 0);
    buzzer_beep(2000, 600);
}

static void next_profile(void)
{
    grow_phase_t p = (climate_get_phase(0) + 1) % (PHASE_OFF + 1);   // inkl. „Aus" (Kammer A)
    climate_set_phase(0, p);
    ESP_LOGI(TAG, "Profil Kammer A → %s", climate_phase_name(p));
    buzzer_beep(2500, 120);
}

static void ack_alarm(void)
{
    climate_alarm_ack();
    ESP_LOGI(TAG, "Alarm quittiert (stumm)");
    buzzer_beep(2200, 50);   // kurze Bestätigung
}

static void enter_wifi_config(void)
{
    ESP_LOGW(TAG, "WLAN-Konfig (Hotspot) wird aktiviert — Neustart in AP-Modus");
    buzzer_beep(1500, 400); buzzer_beep(1500, 400);
    wifi_force_config();   // setzt Flag + Neustart → AP + Captive-Portal
}

static void task(void *arg)
{
    int ac_held = 0, bt_held = 0;
    bool ac_fed = false, bt_fed2 = false, bt_fed10 = false;

    while (true) {
        bool ac = (gpio_get_level(KEY_AC_PIN) == 0);   // active-low
        bool bt = (gpio_get_level(KEY_BT_PIN) == 0);

        // ── KEY_AC (Power): 5 s → Notaus ──
        if (ac) {
            ac_held++;
            if (ac_held == T_POWER && !ac_fed) { buzzer_beep(3000, 80); ac_fed = true; } // "jetzt loslassen"
        } else {
            if (ac_held >= T_POWER) emergency_off();
            ac_held = 0; ac_fed = false;
        }

        // ── KEY_BT: 2 s → Profil, 10 s → Reset ──
        if (bt) {
            bt_held++;
            if (bt_held == T_BT_PROF && !bt_fed2)  { buzzer_beep(2800, 60); bt_fed2 = true; }
            if (bt_held == T_BT_RST  && !bt_fed10) { buzzer_beep(1800, 200); bt_fed10 = true; }
        } else {
            if (bt_held >= T_BT_RST)        enter_wifi_config();   // >=10 s → Hotspot/WLAN-Konfig
            else if (bt_held >= T_BT_PROF)  next_profile();    // 2..10 s
            else if (bt_held >= 2)          ack_alarm();       // kurzer Tipp (0,2..2 s)
            bt_held = 0; bt_fed2 = false; bt_fed10 = false;
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t buttons_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << KEY_AC_PIN) | (1ULL << KEY_BT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
    xTaskCreate(task, "buttons", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "Taster init (Power 5s=Notaus, BT 2s=Profil, BT 10s=Reset)");
    return ESP_OK;
}
