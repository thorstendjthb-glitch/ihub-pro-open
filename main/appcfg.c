// appcfg.c — Laufzeit-Konfiguration (NVS) für MQTT-Broker
#include "appcfg.h"
#include "app_config.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>   // setenv
#include <time.h>     // tzset

#define TZ_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Berlin inkl. Sommerzeit

static char s_uri[96], s_user[48], s_pass[48], s_token[48], s_host[32], s_tz[48];
static char s_ap_pass[24];      // Hotspot-Passwort (pro Gerät zufällig, persistiert)
static uint8_t s_protect = 0;   // 1 = API-Schutz auch für Steuer-Endpunkte (opt-in)

static void apply_tz(void)
{
    setenv("TZ", s_tz[0] ? s_tz : TZ_DEFAULT, 1);
    tzset();   // wirkt sofort auf localtime_r() → Photoperiode/Zeitpläne in Ortszeit
}

static void load_str(nvs_handle_t h, const char *key, char *dst, size_t dsz, const char *def)
{
    size_t sz = dsz;
    if (nvs_get_str(h, key, dst, &sz) != ESP_OK) { strncpy(dst, def, dsz - 1); dst[dsz-1] = 0; }
}

esp_err_t appcfg_init(void)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
        load_str(h, "mqtt_uri",  s_uri,  sizeof(s_uri),  CFG_MQTT_URI);
        load_str(h, "mqtt_user", s_user, sizeof(s_user), CFG_MQTT_USER);
        load_str(h, "mqtt_pass", s_pass, sizeof(s_pass), CFG_MQTT_PASS);
        load_str(h, "api_token", s_token, sizeof(s_token), "");   // Default: kein Schutz
        load_str(h, "host", s_host, sizeof(s_host), "ihub");      // mDNS-Hostname
        load_str(h, "tz",   s_tz,   sizeof(s_tz),   TZ_DEFAULT);  // Zeitzone (POSIX-TZ)
        load_str(h, "ap_pw", s_ap_pass, sizeof(s_ap_pass), "");   // Hotspot-PW (leer → lazy-gen)
        nvs_get_u8(h, "protect", &s_protect);                     // Default 0 (offen)
        nvs_close(h);
    } else {
        strcpy(s_uri, CFG_MQTT_URI); strcpy(s_user, CFG_MQTT_USER); strcpy(s_pass, CFG_MQTT_PASS);
        s_token[0] = 0;
        strcpy(s_host, "ihub");
        strcpy(s_tz, TZ_DEFAULT);
    }
    apply_tz();   // Zeitzone setzen, bevor irgendeine Zeitlogik läuft
    ESP_LOGI("appcfg", "MQTT-Broker: %s | TZ: %s", s_uri, s_tz);
    return ESP_OK;
}

const char *appcfg_mqtt_uri(void)  { return s_uri; }
const char *appcfg_mqtt_user(void) { return s_user; }
const char *appcfg_mqtt_pass(void) { return s_pass; }

void appcfg_set_mqtt(const char *uri, const char *user, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        if (uri)  { nvs_set_str(h, "mqtt_uri",  uri);  strncpy(s_uri,  uri,  sizeof(s_uri)-1);  s_uri[sizeof(s_uri)-1]=0; }
        if (user) { nvs_set_str(h, "mqtt_user", user); strncpy(s_user, user, sizeof(s_user)-1); s_user[sizeof(s_user)-1]=0; }
        if (pass) { nvs_set_str(h, "mqtt_pass", pass); strncpy(s_pass, pass, sizeof(s_pass)-1); s_pass[sizeof(s_pass)-1]=0; }
        nvs_commit(h); nvs_close(h);
        ESP_LOGI("appcfg", "MQTT-Settings gespeichert: %s", s_uri);
    }
}

const char *appcfg_api_token(void) { return s_token; }

// Hotspot-Passwort: pro Gerät einmalig zufällig erzeugt und persistiert (statt hartem
// Default auf jedem Gerät). Lazy bei erstem Aufruf (zu dem Zeitpunkt ist der RNG geseedet).
const char *appcfg_ap_pass(void)
{
    if (!s_ap_pass[0]) {
        snprintf(s_ap_pass, sizeof(s_ap_pass), "iHub-%08lX%04lX",
                 (unsigned long)esp_random(), (unsigned long)(esp_random() & 0xFFFF));
        nvs_handle_t h;
        if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "ap_pw", s_ap_pass); nvs_commit(h); nvs_close(h);
            ESP_LOGW("appcfg", "Hotspot-Passwort generiert (siehe Einstellungen): %s", s_ap_pass);
        }
    }
    return s_ap_pass;
}

const char *appcfg_hostname(void) { return s_host; }

const char *appcfg_tz(void) { return s_tz; }

void appcfg_set_tz(const char *tz)
{
    if (!tz || !tz[0]) return;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "tz", tz);
        strncpy(s_tz, tz, sizeof(s_tz) - 1); s_tz[sizeof(s_tz) - 1] = 0;
        nvs_commit(h); nvs_close(h);
        apply_tz();   // sofort wirksam (kein Reboot nötig)
        ESP_LOGI("appcfg", "Zeitzone → %s", s_tz);
    }
}

void appcfg_set_hostname(const char *host)
{
    if (!host || !host[0]) return;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "host", host);
        strncpy(s_host, host, sizeof(s_host) - 1); s_host[sizeof(s_host) - 1] = 0;
        nvs_commit(h); nvs_close(h);
        ESP_LOGI("appcfg", "Hostname → %s.local (wirkt nach Reboot)", s_host);
    }
}

bool appcfg_api_protect(void) { return s_protect != 0; }

void appcfg_set_api_protect(bool on)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        s_protect = on ? 1 : 0;
        nvs_set_u8(h, "protect", s_protect);
        nvs_commit(h); nvs_close(h);
        ESP_LOGI("appcfg", "API-Schutz Steuer-Endpunkte: %s", on ? "AN" : "aus");
    }
}

void appcfg_set_api_token(const char *token)
{
    if (!token) return;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "api_token", token);
        strncpy(s_token, token, sizeof(s_token)-1); s_token[sizeof(s_token)-1] = 0;
        nvs_commit(h); nvs_close(h);
        ESP_LOGI("appcfg", "API-Token %s", token[0] ? "gesetzt" : "gelöscht");
    }
}
