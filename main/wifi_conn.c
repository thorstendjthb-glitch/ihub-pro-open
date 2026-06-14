// wifi_conn.c — WLAN-Station mit Reconnect + AP-Config-Modus (Captive-Portal)
#include "wifi_conn.h"
#include "app_config.h"
#include "appcfg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "wifi";
static EventGroupHandle_t s_evt;
static const int CONNECTED_BIT = BIT0;
static volatile bool s_connected = false;
static bool s_ap_mode = false;
static char s_ssid[33];
static char s_pass[65];

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_evt, CONNECTED_BIT);
        if (!s_ap_mode) {                      // im AP-Modus nicht dauernd reconnecten
            ESP_LOGW(TAG, "getrennt, reconnect ...");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "verbunden, IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_evt, CONNECTED_BIT);
    }
}

// Zugangsdaten laden: NVS bevorzugt, sonst Default aus app_config.h.
static void load_creds(void)
{
    strncpy(s_ssid, CFG_WIFI_SSID, sizeof(s_ssid) - 1);
    strncpy(s_pass, CFG_WIFI_PASSWORD, sizeof(s_pass) - 1);

    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        char ns[33] = {0}, np[65] = {0};
        size_t sl = sizeof(ns), pl = sizeof(np);
        if (nvs_get_str(h, "ssid", ns, &sl) == ESP_OK && ns[0]) {
            strncpy(s_ssid, ns, sizeof(s_ssid) - 1); s_ssid[sizeof(s_ssid)-1] = 0;
            if (nvs_get_str(h, "pass", np, &pl) == ESP_OK)
                strncpy(s_pass, np, sizeof(s_pass) - 1);
            else
                s_pass[0] = 0;
            s_pass[sizeof(s_pass)-1] = 0;
        }
        nvs_close(h);
    }
}

static bool force_config_flag(void)
{
    nvs_handle_t h; uint8_t v = 0;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "cfg", &v);
        nvs_close(h);
    }
    return v != 0;
}

static void set_config_flag(uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "cfg", v);
        nvs_commit(h); nvs_close(h);
    }
}

void wifi_force_config(void)
{
    ESP_LOGW(TAG, "Config-Modus erzwungen → Neustart in AP-Modus");
    set_config_flag(1);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

void wifi_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid ? ssid : "");
        nvs_set_str(h, "pass", pass ? pass : "");
        nvs_set_u8(h, "cfg", 0);            // nach Speichern wieder normaler STA-Boot
        nvs_commit(h); nvs_close(h);
        ESP_LOGI(TAG, "WLAN-Zugangsdaten gespeichert: '%s'", ssid ? ssid : "");
    }
}

const char *wifi_current_ssid(void) { return s_ssid; }
bool wifi_is_connected(void)        { return s_connected; }
bool wifi_is_ap_mode(void)          { return s_ap_mode; }

// Permanenter Parallelbetrieb: STA (Heim-WLAN) + eigener WPA2-Hotspot.
// Der Hotspot erlaubt direkten Zugriff aufs Dashboard (http://192.168.4.1/),
// auch wenn das Heimnetz den Zugriff Smartphone→iHub blockiert.
static void start_sta(void)
{
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, s_pass, sizeof(wc.sta.password));

    wifi_config_t ap = { 0 };
    const char *apssid = "iHub";
    const char *appass = appcfg_ap_pass();       // pro Gerät zufällig (kein hartes Default-PW!)
    strncpy((char *)ap.ap.ssid, apssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len       = strlen(apssid);
    strncpy((char *)ap.ap.password, appass, sizeof(ap.ap.password));
    ap.ap.max_connection = 4;
    ap.ap.authmode       = WIFI_AUTH_WPA2_PSK;   // PW pro Gerät (siehe Einstellungen/Log)
    ap.ap.channel        = 1;                    // folgt automatisch dem STA-Kanal

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "APSTA: STA '%s' + Hotspot 'iHub' (http://192.168.4.1/)", s_ssid);
    xEventGroupWaitBits(s_evt, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
}

static void start_ap(void)
{
    s_ap_mode = true;
    const char *apssid = "iHub-Pro-Setup";
    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, apssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len      = strlen(apssid);
    ap.ap.channel       = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode      = WIFI_AUTH_OPEN;     // offenes Setup-Netz
    // APSTA: AP für die Konfig-Seite, STA-Interface aktiv fürs Netzwerk-Scannen.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG, "AP-Config-Modus aktiv: SSID '%s' → http://192.168.4.1/", apssid);
}

esp_err_t wifi_start(void)
{
    s_evt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    bool cfg = force_config_flag();
    load_creds();
    bool ap = cfg || (s_ssid[0] == 0);

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();   // immer: für Config-Modus UND permanenten Hotspot

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL));

    if (ap) {
        set_config_flag(0);   // Flag verbrauchen, damit nächster Boot wieder STA versucht
        start_ap();
    } else {
        start_sta();
    }
    return ESP_OK;
}
