// app_main.c — Mars Hydro iHub-Pro Cannabis-Grow-Controller (ESP-IDF)
//
// Architektur:
//   climate_task (in climate_control.c) = EINZIGER Modbus-Bus-Nutzer:
//       pollt Sensoren + BL0940 → State-Cache → reguliert Aktoren.
//   publish_task (hier)  = liest State-Cache → MQTT (HA).
//   webui (httpd)        = liest State-Cache → Dashboard/JSON.
#include <stdio.h>
#include <stdlib.h>   // setenv
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

#include "board_pins.h"
#include "appcfg.h"
#include "state.h"
#include "relays.h"
#include "relay_sched.h"
#include "dimmer.h"
#include "devices.h"
#include "bl0940.h"
#include "ds1302.h"
#include "buttons.h"
#include "buzzer.h"
#include "status_leds.h"
#include "oled.h"
#include "climate_control.h"
#include "wifi_conn.h"
#include "wifi_portal.h"
#include "ble_sensors.h"
#include "mqtt_ha.h"
#include "ota.h"
#include "auth.h"
#include "webui.h"
#include "remote_log.h"
#include "mdns.h"

static const char *TAG = "app";

// ── App-Level Brick-Schutz (wirkt per OTA, unabhängig vom Bootloader-Rollback) ──
// Zählt Boot-Versuche im RTC-RAM (überlebt SW-Reboot/WDT-Reset, NICHT Power-Cycle).
// Die App markiert sich erst nach BOOT_STABLE_S Sekunden stabilem Lauf als gültig und
// setzt den Zähler zurück. Crasht ein frisch per OTA geschriebenes Image immer wieder
// beim Start, schaltet der Validator nach BOOT_MAX_TRY Versuchen automatisch auf die
// andere OTA-Partition (= vorherige Firmware) zurück → kein Aufschrauben nötig.
#define BOOT_MAGIC    0xB007C0DEu
#define BOOT_MAX_TRY  4
#define BOOT_STABLE_S 60
RTC_NOINIT_ATTR static uint32_t s_boot_magic;
RTC_NOINIT_ATTR static uint32_t s_boot_tries;

// Früh im Boot: Zähler hochzählen, bei Dauer-Crash auf andere Partition zurückfallen.
static void boot_guard_enter(void)
{
    if (s_boot_magic != BOOT_MAGIC) { s_boot_magic = BOOT_MAGIC; s_boot_tries = 0; }
    s_boot_tries++;
    if (s_boot_tries > BOOT_MAX_TRY) {
        const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
        if (other) {
            ESP_LOGE(TAG, "Boot-Validator: %lu Fehlversuche → Rückfall auf '%s'",
                     (unsigned long)s_boot_tries, other->label);
            s_boot_tries = 0;
            if (esp_ota_set_boot_partition(other) == ESP_OK) { esp_restart(); }
        }
        s_boot_tries = 0;   // andere Partition nicht setzbar → Zähler entschärfen, normal weiter
    }
}

// Nach stabilem Lauf: Image als gültig markieren (Rollback abbestellen) + Zähler nullen.
static void boot_guard_mark_ok(void)
{
    s_boot_tries = 0;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Boot-Validator: stabil seit %ds → Image gültig, Rollback abbestellt", BOOT_STABLE_S);
    } else {
        ESP_LOGI(TAG, "Boot-Validator: stabil seit %ds → Boot-Zähler zurückgesetzt", BOOT_STABLE_S);
    }
}

// MQTT-Publish aus dem State-Cache (kein Bus-Zugriff hier!)
static void publish_task(void *arg)
{
    sensor_data_t s; power_data_t p; climate_status_t c;
    while (true) {
        if (wifi_is_connected()) {
            state_get_sensors(&s);
            state_get_power(&p);
            state_get_climate(&c);
            mqtt_ha_publish_sensors(&s);
            mqtt_ha_publish_power(&p);
            mqtt_ha_publish_climate(&c);
            mqtt_ha_publish_outputs();   // Relais/Dimmer: nur Änderungen (Diff, retained)
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// Zeit: SNTP (primär) — DS1302 als Backup. Bei SNTP-Erfolg RTC nachziehen.
static void time_sync_task(void *arg)
{
    // Erst RTC → Systemzeit (falls kein WLAN)
    struct tm rtc;
    if (ds1302_get(&rtc)) {
        time_t t = mktime(&rtc);
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Zeit aus DS1302 übernommen");
    }
    // SNTP starten
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    // Auf Sync warten, dann RTC nachziehen
    for (int i = 0; i < 30; i++) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
            ds1302_set(&tm);
            ESP_LOGI(TAG, "SNTP synchronisiert, DS1302 aktualisiert");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== iHub-Pro Grow Controller === Build %s %s", __DATE__, __TIME__);

    boot_guard_enter();   // Brick-Schutz: Dauer-Crash → Rückfall auf vorherige Partition
    // Zeitzone wird in appcfg_init() aus NVS gesetzt (setenv+tzset), bevor Zeitlogik läuft.

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // ── Hardware ──
    ESP_ERROR_CHECK(appcfg_init());     // MQTT-Broker aus NVS/Defaults
    auth_init();                        // WebUI-Login (Benutzer/Passwort) aus NVS
    ESP_ERROR_CHECK(state_init());
    ESP_ERROR_CHECK(relays_init());
    relay_sched_init();                 // Steckdosen-Zeitpläne aus NVS + Claims setzen
    ESP_ERROR_CHECK(dimmer_init());
    ESP_ERROR_CHECK(devices_init());
    ESP_ERROR_CHECK(bl0940_init());
    ESP_ERROR_CHECK(ds1302_init());
    ESP_ERROR_CHECK(buzzer_init());
    buttons_init();
    oled_init();                        // OLED-Statusdisplay (No-Op solange OLED_ENABLED=0)
    ESP_ERROR_CHECK(climate_init());

    // ── Netzwerk + Dienste ──
    wifi_start();
    // mDNS: Gerät als http://<hostname>.local/ erreichbar (Hostname in /settings änderbar).
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(appcfg_hostname());
        mdns_instance_name_set("Mars Hydro iHub-Pro");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS aktiv: http://%s.local/", appcfg_hostname());
    } else {
        ESP_LOGW(TAG, "mDNS-Init fehlgeschlagen (Dashboard weiter per IP erreichbar)");
    }
    ble_sensors_start();                // passiver BLE-Scan (Funk-Klimasensoren)
    status_leds_start();                // Power/BT-LEDs (WLAN-Status / Alarm)
    remote_log_start();                 // TCP Port 23 — Live-Logs
    xTaskCreate(time_sync_task, "timesync", 4096, NULL, 3, NULL);

    httpd_handle_t srv = NULL;
    ota_start(&srv);                    // HTTP Port 80 (OTA /update + /ota)
    if (srv) {
        auth_web_register(srv);         // /login + /api/login + /api/logout
        webui_register(srv);            // Dashboard "/" + JSON-API
        wifi_portal_register(srv);      // /wifi WLAN-Konfig (auch im STA-Modus erreichbar)
    }
    if (wifi_is_ap_mode()) {
        if (srv) wifi_portal_start_captive(srv);   // DNS-Hijack + Redirect
        ESP_LOGW(TAG, "AP-Config-Modus aktiv → MQTT übersprungen");
    } else if (appcfg_mqtt_uri()[0]) {
        mqtt_ha_start();
    } else {
        ESP_LOGW(TAG, "Kein MQTT-Broker konfiguriert → Home-Assistant-Anbindung aus (in /settings setzbar)");
    }

    // ── Regelung + Publish ──
    climate_start();                    // pollt Bus + reguliert + State
    oled_start();                       // OLED-Render-Task (No-Op solange OLED_ENABLED=0)
    xTaskCreate(publish_task, "mqtt_pub", 6144, NULL, 4, NULL);   // 6144: MQTT-Publish + JSON-Aufbau

    ESP_LOGI(TAG, "Bereit. Dashboard: http://<ip>/  ·  OTA: /update  ·  Log: nc <ip> 23");

    // Brick-Schutz: erst nach stabilem Lauf das (ggf. frisch per OTA geschriebene) Image
    // als gültig markieren — vorher würde ein Frühcrash sonst nie zurückgerollt.
    int64_t t0 = esp_timer_get_time();
    bool validated = false;
    while (true) {
        if (!validated && (esp_timer_get_time() - t0) > (int64_t)BOOT_STABLE_S * 1000000LL) {
            validated = true;
            boot_guard_mark_ok();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
