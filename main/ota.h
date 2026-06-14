// ota.h — Remote-Firmware-Update über WLAN (HTTP-Upload) + Status-Seite
//
// Startet einen HTTP-Server:
//   GET  /        → Status + Upload-Formular
//   POST /ota     → neue .bin in inaktive OTA-Partition schreiben, dann Reboot
//   GET  /reboot  → Neustart
// Damit ist nach dem ersten USB-Flash kein Kabel mehr nötig.
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

// Startet den HTTP-Server und registriert OTA-Handler.
// Gibt das Handle zurück, damit WebUI später weitere URIs anhängen kann.
esp_err_t ota_start(httpd_handle_t *out_server);
