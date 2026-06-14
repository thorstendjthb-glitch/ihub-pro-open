// webui_control.c — WebUI-Teilmodul: Aktor-/Grow-Steuerung
// (Relais, Lichter, Phasen, Kammer-Zuordnung, iFan/Befeuchter/Clip-Fan, Grow-Zyklus,
//  RTC-Diagnose, Langzeit-Verlauf). Registriert via webui_register() in webui.c.
#include "webui_internal.h"
#include "relays.h"
#include "dimmer.h"
#include "climate_control.h"
#include "ds1302.h"
#include "history.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static esp_err_t relay_post(httpd_req_t *req)
{
    // ?id=&on=    manuelles Schalten (setzt MANUAL-Override)
    // ?id=&auto=1 Kanal zurück auf Klimaregelung (AUTO)
    int id = webui_qint(req, "id", -1);
    if (id < 0 || id >= RLY_MAX) return httpd_resp_sendstr(req, "err");
    if (webui_qint(req, "auto", 0)) {
        relay_set_mode(id, RMODE_AUTO);
    } else {
        relay_set_manual(id, webui_qint(req, "on", 0) != 0);
    }
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t light_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", -1), pct = webui_qint(req, "pct", 0);
    if (ch == 0 || ch == 1) dimmer_set_manual(ch, pct);   // manueller Befehl → persistent
    return httpd_resp_sendstr(req, "ok");
}

// 10-Stufen-Slider (leistungs-kalibriert): /api/lightstep?ch=0|1&step=0..10
static esp_err_t lightstep_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", -1), step = webui_qint(req, "step", 0);
    if (ch == 0 || ch == 1) dimmer_set_step(ch, step);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t phase_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", 0);          // Kammer (Default A)
    int p  = webui_qint(req, "p", -1);
    if (ch < 0 || ch >= N_CHAMBERS) ch = 0;
    if (p >= 0 && p <= PHASE_OFF) climate_set_phase(ch, p);
    return httpd_resp_sendstr(req, "ok");
}

// Klima-Automatik je Kammer ein/aus — /api/chamber?ch=0|1&auto=0|1
// auto=0 → Kammer läuft nur manuell (keine geregelten Aktoren), Messwerte bleiben sichtbar.
// SCHUTZ: Abschalten NUR mit zusätzlichem confirm=1 — ohne Automatik sind Profile/
// Regelung komplett wirkungslos und das fiel mangels Anzeige lange nicht auf
// (Vorfall 2026-06-12, siehe SESSION_LOG). Einschalten bleibt ungeschützt.
static esp_err_t chamber_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", 0);
    if (ch < 0 || ch >= N_CHAMBERS) ch = 0;
    bool on = webui_qint(req, "auto", 1) != 0;
    if (!on && !webui_qint(req, "confirm", 0)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "err: Automatik-AUS nur mit confirm=1 (Schutz vor versehentlichem Abschalten)");
    }
    climate_set_chamber_auto(ch, on);
    return httpd_resp_sendstr(req, "ok");
}

// Dimmer-Nullpunkt kalibrieren — /api/dimcal?ch=0|1&min=0..99
// min = Ausgang in % bei Slider 0 (Lampe geht real bei ~7 % aus → min=7).
static esp_err_t dimcal_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", -1);
    int mn = webui_qint(req, "min", -1);
    if (ch < 0 || ch > 1 || mn < 0) return httpd_resp_sendstr(req, "err");
    dimmer_set_cal((dim_ch_t)ch, (uint8_t)mn);
    return httpd_resp_sendstr(req, "ok");
}

// 2-Kammer-Zuordnung (kombiniert) — /api/assign
//   ?relay=<0..9>&ch=<-1..1>&role=<0..9>   Steckdose einer Kammer+Rolle zuweisen
//   ?dim=<0|1>&ch=<-1..1>                  Dimmer einer Kammer zuweisen
//   ?ifan=1&ch=<-1..1>                     stufenlosen Abluft-iFan einer Kammer zuweisen
//   ?csens=<0..2>&src=<0..2>[&mac=HEX12]   Klimasensor-Quelle setzen (0/1=Kammer A/B, 2=Dachboden)
//     src: 0=Sensor-Bus-TH3in1 1=Fan-Bus-TH3in1 2=BLE(TP357 per MAC, Anzeige-Reihenfolge)
static esp_err_t assign_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", 0);
    int relay = webui_qint(req, "relay", -1);
    if (relay >= 0 && relay < RLY_MAX) {
        int role = webui_qint(req, "role", relay_role(relay));
        relay_assign(relay, (int8_t)ch, (func_id_t)role);
    }
    int dim = webui_qint(req, "dim", -1);
    if (dim == 0 || dim == 1) dim_set_chamber(dim, (int8_t)ch);
    if (webui_qint(req, "ifan", 0)) ifan_set_chamber((int8_t)ch);
    int csens = webui_qint(req, "csens", -1);
    if (csens >= 0 && csens < N_SENS) {   // 0/1 = Kammer A/B, CSENS_ATTIC(2) = Dachboden
        int src = webui_qint(req, "src", 0);
        uint8_t mac[6] = {0}; bool hasmac = false;
        char q[160], v[16];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "mac", v, sizeof(v)) == ESP_OK && strlen(v) >= 12) {
            // 12 Hex-Zeichen in Anzeige-Reihenfolge (MSB zuerst) → addr.val-Reihenfolge umdrehen
            for (int i = 0; i < 6; i++) {
                char b[3] = { v[i*2], v[i*2+1], 0 };
                mac[5 - i] = (uint8_t)strtol(b, NULL, 16);
            }
            hasmac = true;
        }
        climate_set_sensor(csens, (sensor_src_t)src, hasmac ? mac : NULL);
    }
    return httpd_resp_sendstr(req, "ok");
}

// Diagnose RTC (DS1302) — /api/rtc: liest RTC-Rohzeit + vergleicht mit Systemzeit.
// rtc_ok=false → Clock-Halt-Bit gesetzt (Uhr läuft nicht / nie gestellt / Backup leer).
static esp_err_t rtc_get(httpd_req_t *req)
{
    struct tm rt; bool ok = ds1302_get(&rt);
    time_t sysnow = time(NULL); struct tm st; localtime_r(&sysnow, &st);   // Ortszeit (TZ gesetzt)
    char rs[24] = "--", ss[24];
    if (ok) strftime(rs, sizeof(rs), "%Y-%m-%d %H:%M:%S", &rt);
    strftime(ss, sizeof(ss), "%Y-%m-%d %H:%M:%S", &st);
    long diff = 0;
    if (ok) { struct tm c = rt; diff = (long)(mktime(&c) - mktime(&st)); }  // RTC speichert Ortszeit → beide lokal
    char buf[200];
    int n = snprintf(buf, sizeof(buf),
        "{\"rtc_ok\":%s,\"rtc\":\"%s\",\"sys\":\"%s\",\"diff_s\":%ld}",
        ok ? "true" : "false", rs, ss, diff);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// Alarm akustisch quittieren (wie BT-Tipp) — /api/ack
static esp_err_t ack_post(httpd_req_t *req)
{
    climate_alarm_ack();
    return httpd_resp_sendstr(req, "ok");
}

// Abluft-iFan manuell — /api/ifan?pct=0..100  oder  /api/ifan?auto=1 (zurück auf Automatik)
static esp_err_t ifan_post(httpd_req_t *req)
{
    if (webui_qint(req, "auto", 0)) climate_set_ifan_manual(-1);
    else { int p = webui_qint(req, "pct", -1); if (p >= 0) climate_set_ifan_manual(p); }
    return httpd_resp_sendstr(req, "ok");
}

// Luftbefeuchter — /api/humidifier?level=0..4  oder  ?auto=1 (zurück auf VPD-Automatik)
static esp_err_t humid_post(httpd_req_t *req)
{
    if (webui_qint(req, "auto", 0)) climate_set_humid_manual(-1);
    else { int l = webui_qint(req, "level", -1); if (l >= 0) climate_set_humid_manual(l); }
    return httpd_resp_sendstr(req, "ok");
}

// Clip-Fan — /api/clipfan?stufe=0..10&schwenk=0|5|10&natural=0|1 (Direktsteuerung)
static esp_err_t clipfan_post(httpd_req_t *req)
{
    int stufe = webui_qint(req, "stufe", 0), schwenk = webui_qint(req, "schwenk", 0);
    int natural = webui_qint(req, "natural", 0);
    climate_set_clipfan(stufe, schwenk, natural != 0);
    return httpd_resp_sendstr(req, "ok");
}

// Langzeit-Verlauf — /api/history?m=0..5  (0=temp 1=rh 2=vpd 3=co2 4=power 5=ifan)
static esp_err_t history_get_h(httpd_req_t *req)
{
    int m = webui_qint(req, "m", 0); if (m < 0 || m >= HIST_METRICS) m = 0;
    // A: Temp÷10 rH÷10 VPD÷100 · B: dito · CO₂÷1 Leistung÷1 Abluft÷1
    // Wasser: pH÷100 ORP÷1 Wassertemp÷10 EC÷1000(µS→mS) TDS÷1 Salinität÷1 Dichte÷1000
    static const int divs[HIST_METRICS] = {10, 10, 100, 10, 10, 100, 1, 1, 1,
                                           100, 1, 10, 1000, 1, 1, 1000};
    // ?long=1 → grober Langzeit-Ring (1 h, ~120 Tage); sonst feiner 7-Tage-Ring (5 min).
    bool longterm = webui_qint(req, "long", 0) != 0;
    int maxn = longterm ? HIST_LONG_N : HIST_N;
    int16_t *vals = malloc(maxn * sizeof(int16_t));
    if (!vals) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int interval; uint32_t newest;
    int n = history_get(m, vals, maxn, &interval, &newest, longterm);
    int cap = n * 8 + 200;
    char *buf = malloc(cap);
    if (!buf) { free(vals); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int p = snprintf(buf, cap, "{\"m\":%d,\"long\":%d,\"interval\":%d,\"newest\":%lu,\"div\":%d,\"n\":%d,\"v\":[",
                     m, longterm ? 1 : 0, interval, (unsigned long)newest, divs[m], n);
    for (int i = 0; i < n; i++) {
        if (vals[i] == -32768) p += snprintf(buf + p, cap - p, "%snull", i ? "," : "");
        else                   p += snprintf(buf + p, cap - p, "%s%d", i ? "," : "", vals[i]);
    }
    p += snprintf(buf + p, cap - p, "]}");
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, buf, p);
    free(buf); free(vals);
    return e;
}

// Grow-Zyklus setzen — /api/grow?ch=0|1&start=<epoch>&days=<n>
static esp_err_t grow_post(httpd_req_t *req)
{
    int ch = webui_qint(req, "ch", 0); if (ch < 0 || ch >= N_CHAMBERS) ch = 0;
    uint32_t cs; uint16_t cd; climate_get_grow(ch, &cs, &cd);
    int start = webui_qint(req, "start", -1);
    int days  = webui_qint(req, "days", 0);
    climate_set_grow(ch, start >= 0 ? (uint32_t)start : cs, days > 0 ? (uint16_t)days : cd);
    return httpd_resp_sendstr(req, "ok");
}

// GET /api/alarmlog → Alarm-Ereignisprotokoll (neueste zuerst) als JSON
static esp_err_t alarmlog_get(httpd_req_t *req)
{
    static const char *TN[] = { "temp", "mold", "co2", "sensor", "light", "water" };
    alarm_evt_t ev[24];
    int n = climate_alarmlog(ev, 24);
    char buf[1100]; int p = snprintf(buf, sizeof(buf), "[");
    for (int i = 0; i < n; i++)
        p += snprintf(buf + p, sizeof(buf) - p, "%s{\"ts\":%lu,\"type\":\"%s\",\"on\":%d}",
                      i ? "," : "", (unsigned long)ev[i].ts,
                      ev[i].type < 6 ? TN[ev[i].type] : "?", ev[i].on);
    snprintf(buf + p, sizeof(buf) - p, "]");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

// Alle schreibenden Endpunkte laufen über webui_guard (opt-in API-Schutz);
// die Lese-Endpunkte (rtc, history, alarmlog) bleiben offen wie /api/status.
#define GUARDED(u, h) { .uri = (u), .method = HTTP_POST, .handler = webui_guard, .user_ctx = (void *)(h) }
static const httpd_uri_t k_uris[] = {
    GUARDED("/api/relay",      relay_post),
    GUARDED("/api/light",      light_post),
    GUARDED("/api/lightstep",  lightstep_post),
    GUARDED("/api/phase",      phase_post),
    GUARDED("/api/chamber",    chamber_post),
    GUARDED("/api/dimcal",     dimcal_post),
    GUARDED("/api/assign",     assign_post),
    GUARDED("/api/ack",        ack_post),
    GUARDED("/api/ifan",       ifan_post),
    GUARDED("/api/humidifier", humid_post),
    GUARDED("/api/clipfan",    clipfan_post),
    GUARDED("/api/grow",       grow_post),
    { .uri = "/api/rtc",      .method = HTTP_GET, .handler = rtc_get },
    { .uri = "/api/history",  .method = HTTP_GET, .handler = history_get_h },
    { .uri = "/api/alarmlog", .method = HTTP_GET, .handler = alarmlog_get },
};
#undef GUARDED

esp_err_t webui_control_register(httpd_handle_t srv)
{
    for (size_t i = 0; i < sizeof(k_uris) / sizeof(k_uris[0]); i++)
        httpd_register_uri_handler(srv, &k_uris[i]);
    return ESP_OK;
}
