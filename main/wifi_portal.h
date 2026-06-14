// wifi_portal.h — Captive-Portal für WLAN-Konfiguration (AP-Modus)
#pragma once
#include "esp_http_server.h"

// Registriert /wifi (Konfig-Seite), /wifiscan (Netzwerk-Scan), /wifisave (POST).
// Funktioniert in STA- und AP-Modus (im STA-Modus zum WLAN-Wechsel erreichbar).
void wifi_portal_register(httpd_handle_t srv);

// Startet den DNS-Hijack (alle Anfragen → 192.168.4.1) + 404-Redirect, damit
// sich das Captive-Portal am Endgerät automatisch öffnet. Nur im AP-Modus rufen.
void wifi_portal_start_captive(httpd_handle_t srv);
