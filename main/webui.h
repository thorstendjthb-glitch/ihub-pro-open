// webui.h — Grow-Dashboard (clean/professionell) auf dem vorhandenen httpd
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

// Registriert Dashboard- + JSON-API-URIs am bestehenden Server (aus ota_start).
//   GET  /            → Dashboard (HTML)
//   GET  /api/status  → JSON (Sensoren, Klima, Leistung)
//   POST /api/relay   → ?id=&on=
//   POST /api/light   → ?ch=&pct=
//   POST /api/phase   → ?p=
esp_err_t webui_register(httpd_handle_t server);
