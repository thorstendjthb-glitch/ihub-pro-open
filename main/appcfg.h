// appcfg.h — Laufzeit-Konfiguration (NVS), Fallback auf app_config.h-Defines.
// Aktuell: MQTT-Broker. (WLAN bleibt vorerst in app_config.h — AP-Setup TODO.)
#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t appcfg_init(void);     // lädt aus NVS oder Defaults

const char *appcfg_mqtt_uri(void);
const char *appcfg_mqtt_user(void);
const char *appcfg_mqtt_pass(void);

// Speichert MQTT-Broker-Settings in NVS (wirkt nach Reboot bzw. mqtt-Reconnect).
void appcfg_set_mqtt(const char *uri, const char *user, const char *pass);

// OTA-/Reboot-Schutz-Token. Leer = kein Schutz (Default). Wenn gesetzt, müssen /ota und
// /reboot den passenden `?key=`-Parameter mitsenden.
const char *appcfg_api_token(void);
void        appcfg_set_api_token(const char *token);

// mDNS-Hostname (Default "ihub" → http://ihub.local/). Änderung wirkt nach Reboot.
const char *appcfg_hostname(void);
void        appcfg_set_hostname(const char *host);

// Zeitzone als POSIX-TZ-String (Default Europe/Berlin "CET-1CEST,M3.5.0,M10.5.0/3").
// Bestimmt die Ortszeit für Photoperiode/Bewässerung/Steckdosen-Zeitpläne. set wirkt SOFORT.
const char *appcfg_tz(void);
void        appcfg_set_tz(const char *tz);

// Hotspot/AP-Passwort (WPA2). Pro Gerät zufällig erzeugt + persistiert (kein hartes Default-PW).
const char *appcfg_ap_pass(void);

// Opt-in: API-Schutz auch für Steuer-/Settings-Endpunkte (?key= nötig). Greift NUR,
// wenn zusätzlich ein Token gesetzt ist (leeres Token → offen, kein Lockout möglich).
bool appcfg_api_protect(void);
void appcfg_set_api_protect(bool on);
