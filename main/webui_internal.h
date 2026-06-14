// webui_internal.h — modulinterne Schnittstelle zwischen den WebUI-Teilmodulen:
//   webui.c          Dashboard "/" + /api/status, registriert die Teilmodule
//   webui_control.c  Aktor-/Grow-Steuerung (/api/relay, /api/light, …)
//   webui_settings.c Settings-Seite + /api/settings (GET/POST, cJSON)
//   webui_diag.c     Diagnose-/Debug-Endpunkte (/api/mbscan, /api/gpiotest, …)
// NICHT von anderen Modulen einbinden — öffentliche API ist webui.h.
#pragma once
#include "esp_http_server.h"

// liest ?key=... Query-Parameter als int (def, wenn fehlt/unlesbar)
int webui_qint(httpd_req_t *req, const char *key, int def);

// ── Opt-in API-Schutz (appcfg_api_protect + Token) ──
// Geschützte Endpunkte werden mit handler=webui_guard und user_ctx=<echter Handler>
// registriert; der Guard prüft ?key= gegen das Token und liefert sonst HTTP 401.
// Offen bleiben: "/" , /settings (HTML), /api/status (das Dashboard bleibt lesbar).
esp_err_t webui_guard(httpd_req_t *req);

esp_err_t webui_control_register(httpd_handle_t srv);
esp_err_t webui_settings_register(httpd_handle_t srv);
esp_err_t webui_diag_register(httpd_handle_t srv);
