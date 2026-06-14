// wifi_conn.h — WLAN: Station (Creds aus NVS/Default) ODER AP-Config-Modus
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Startet WLAN: normalerweise als Station mit Zugangsdaten aus NVS (Fallback:
// app_config.h). Ohne gültige Daten oder bei erzwungenem Config-Modus (BT 10s)
// öffnet sich stattdessen ein Setup-Hotspot (AP) + Captive-Portal.
esp_err_t wifi_start(void);

bool wifi_is_connected(void);     // STA verbunden?
bool wifi_is_ap_mode(void);       // läuft im AP-Config-Modus?
const char *wifi_current_ssid(void);

// WLAN-Zugangsdaten in NVS speichern (vom Captive-Portal).
void wifi_save_creds(const char *ssid, const char *pass);

// Config-Modus erzwingen (BT 10s): Flag in NVS setzen + Neustart → AP-Modus.
void wifi_force_config(void);
