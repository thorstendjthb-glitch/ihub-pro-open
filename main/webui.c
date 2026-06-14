// webui.c — Grow-Dashboard + JSON-API (Kern: "/" + /api/status).
// Die weiteren Endpunkte liegen in den Teilmodulen webui_control.c /
// webui_settings.c / webui_diag.c und werden hier mitregistriert.
#include "webui.h"
#include "webui_internal.h"
#include "webui_html_gz.h"
#include "i18n_js.h"
#include "state.h"
#include "relays.h"
#include "dimmer.h"
#include "climate_control.h"
#include "watering.h"
#include "relay_sched.h"
#include "appcfg.h"
#include "auth.h"
#include "esp_log.h"
#include <string.h>
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "webui";

static esp_err_t dash_get(httpd_req_t *req)
{
    if (auth_redirect_if_needed(req)) return ESP_OK;   // nicht eingeloggt → /login
    // gzip-komprimiert ausliefern → ~7 KB statt ~20 KB, lädt auch über schwaches WLAN.
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)DASHBOARD_GZ, DASHBOARD_GZ_LEN);
}

// Serves the i18n translation script (German source → English at runtime). Open: it
// contains only UI strings, nothing sensitive, and is needed before any login page.
static esp_err_t i18n_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");  // immer aktuell (sonst hängt alte Übersetzung im Cache)
    return httpd_resp_send(req, I18N_JS, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    sensor_data_t s; power_data_t p; climate_status_t c;
    state_get_sensors(&s); state_get_power(&p); state_get_climate(&c);

    uint32_t gstart = 0; uint16_t gdays = 0;
    climate_get_grow(0, &gstart, &gdays);
    int clip_st = 0, clip_sw = 0; bool clip_nat = false;
    climate_get_clipfan(&clip_st, &clip_sw, &clip_nat);
    bool wat_on = false; uint32_t wat_last = 0, wat_next = 0;
    watering_status(&wat_on, &wat_last, &wat_next);

    // Grüne Idealbänder der aktiven Phase (Kammer A) für die Einfärbung im Dashboard.
    float vpd_lo = 0.8f, vpd_hi = 1.2f;       // VPD = Soll ± Deadband
    float temp_lo = 22.0f, temp_hi = 26.0f;   // Temp = Tag/Nacht-Soll ± Deadband
    float rh_lo = 50.0f, rh_hi = 60.0f;       // Feuchte = Soll ± 5 %
    phase_setpoints_t *asp = climate_setpoints(c.phase);
    if (asp) {
        vpd_lo = asp->vpd_target - asp->vpd_deadband;
        vpd_hi = asp->vpd_target + asp->vpd_deadband;
        float t_tgt  = c.is_day ? asp->temp_day : asp->temp_night;
        temp_lo = t_tgt - asp->temp_deadband; temp_hi = t_tgt + asp->temp_deadband;
        float rh_tgt = c.is_day ? asp->rh_day : asp->rh_night;
        rh_lo = rh_tgt - 5.0f; rh_hi = rh_tgt + 5.0f;
    }

    // statisch statt Stack: httpd arbeitet single-threaded, Handler laufen sequenziell —
    // so kostet der größere Puffer (Kammern + Bewässerung + Wasserwerte) keinen httpd-Task-Stack.
    static char buf[3456];
    int n = snprintf(buf, sizeof(buf),
        "{\"build\":\"%s %s\",\"psram\":%d,\"phase\":%d,\"is_day\":%s,\"dli\":%.2f,\"ifan\":%d,\"ifan_man\":%d,"
        "\"humid_man\":%d,\"clip_stufe\":%d,\"clip_schwenk\":%d,\"clip_nat\":%d,"
        "\"wat_on\":%d,\"wat_last\":%lu,\"wat_next\":%lu,\"wat_mode\":%d,\"dli_tgt\":%.1f,"
        "\"rsched\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"vpd_lo\":%.2f,\"vpd_hi\":%.2f,\"temp_lo\":%.1f,\"temp_hi\":%.1f,\"rh_lo\":%.0f,\"rh_hi\":%.0f,"
        "\"now\":%lu,\"gstart\":%lu,\"gdays\":%d,"
        "\"alarm_temp\":%s,\"alarm_mold\":%s,\"alarm_co2\":%s,\"alarm_sensor\":%s,\"alarm_light\":%s,\"alarm_water\":%s,"
        "\"th_ok\":%s,\"co2_ok\":%s,\"par_ok\":%s,\"soil_ok\":%s,\"soilt_ok\":%s,\"pw_ok\":%s,"
        "\"temp\":%.1f,\"rh\":%.1f,\"vpd\":%.2f,\"co2\":%d,\"co2_raw\":%d,\"co2_base\":%d,\"ppfd\":%d,"
        "\"soil_t\":%.1f,\"soil_m\":%.1f,\"soil_ec\":%.2f,"
        "\"attic_ok\":%s,\"attic_t\":%.1f,\"attic_rh\":%.1f,\"ac_demand\":%s,\"ac_mode\":%d,"
        "\"power\":%.1f,\"energy\":%.3f,\"voltage\":%.1f,\"current\":%.2f,"
        "\"light\":[%d,%d],\"lstep\":[%d,%d],\"relay\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"rmode\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]}",
        __DATE__, __TIME__, (int)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        c.phase, c.is_day ? "true" : "false", c.dli_today, c.ifan_pct, climate_get_ifan_manual(),
        climate_get_humid_manual(), clip_st, clip_sw, clip_nat ? 1 : 0,
        wat_on ? 1 : 0, (unsigned long)wat_last, (unsigned long)wat_next,
        watering_cfg()->mode, climate_dli_target(c.phase),
        relay_sched(0)->mode, relay_sched(1)->mode, relay_sched(2)->mode,
        relay_sched(3)->mode, relay_sched(4)->mode, relay_sched(5)->mode,
        relay_sched(6)->mode, relay_sched(7)->mode, relay_sched(8)->mode,
        relay_sched(9)->mode,
        vpd_lo, vpd_hi, temp_lo, temp_hi, rh_lo, rh_hi,
        (unsigned long)time(NULL), (unsigned long)gstart, gdays,
        c.alarm_temp ? "true" : "false", c.alarm_mold ? "true" : "false",
        c.alarm_co2 ? "true" : "false", c.alarm_sensor ? "true" : "false",
        c.alarm_light ? "true" : "false", c.alarm_water ? "true" : "false",
        s.th3in1_valid ? "true" : "false", s.co2_valid ? "true" : "false",
        s.par_valid ? "true" : "false", s.substrate_valid ? "true" : "false",
        s.soil_temp_valid ? "true" : "false",
        p.valid ? "true" : "false",
        s.temperature_c, s.humidity_pct, c.vpd_now, s.co2_ppm, s.co2_raw, s.co2_base, s.ppfd,
        s.soil_temp_c, s.soil_moist_pct, s.soil_ec,
        s.ref_valid ? "true" : "false", s.ref_temp_c, s.ref_humidity_pct,
        c.ac_demand ? "true" : "false", c.ac_mode,
        p.power_w, p.energy_kwh, p.voltage_v, p.current_a,
        dimmer_get(0), dimmer_get(1),
        dimmer_get_step(0), dimmer_get_step(1),
        relay_get(0), relay_get(1), relay_get(2), relay_get(3), relay_get(4),
        relay_get(5), relay_get(6), relay_get(7), relay_get(8), relay_get(9),
        relay_get_mode(0), relay_get_mode(1), relay_get_mode(2), relay_get_mode(3), relay_get_mode(4),
        relay_get_mode(5), relay_get_mode(6), relay_get_mode(7), relay_get_mode(8), relay_get_mode(9));

    // ── Wasserwerte (von HA via MQTT) + Pro-Kammer-Zustand anhängen ──
    if (n > 0 && n < (int)sizeof(buf)) {
        n--;  // letzte '}' verwerfen
        water_data_t wd; state_get_water(&wd);
        n += snprintf(buf + n, sizeof(buf) - n,
            ",\"water\":{\"fresh\":%s,\"ph\":%.2f,\"ph_ok\":%s,\"ec\":%.2f,\"ec_ok\":%s,"
            "\"tds\":%.0f,\"tds_ok\":%s,\"orp\":%.0f,\"orp_ok\":%s,\"temp\":%.1f,\"temp_ok\":%s,"
            "\"sal\":%.0f,\"sal_ok\":%s,\"sg\":%.3f,\"sg_ok\":%s}",
            water_is_fresh(&wd, 180) ? "true" : "false",
            wd.ph, wd.ph_valid ? "true" : "false",
            wd.ec_us / 1000.0f, wd.ec_valid ? "true" : "false",   // µS/cm → mS/cm
            wd.tds_ppm, wd.tds_valid ? "true" : "false",
            wd.orp_mv, wd.orp_valid ? "true" : "false",
            wd.temp_c, wd.temp_valid ? "true" : "false",
            wd.salinity_ppm, wd.salinity_valid ? "true" : "false",
            wd.sg, wd.sg_valid ? "true" : "false");
        n += snprintf(buf + n, sizeof(buf) - n, ",\"chambers\":[");
        for (int ci = 0; ci < N_CHAMBERS; ci++) {
            chamber_state_t cc; climate_chamber(ci, &cc);
            sensor_src_t src = SRC_TH_SENSOR; climate_get_sensor(ci, &src, NULL);
            uint32_t cgs = 0; uint16_t cgd = 90; climate_get_grow(ci, &cgs, &cgd);   // Grow je Kammer
            // Idealbänder dieser Kammer (für die Ampel-Einfärbung) aus ihrer Phase
            float cvlo = 0.8f, cvhi = 1.2f, ctlo = 22.0f, cthi = 26.0f, crlo = 50.0f, crhi = 60.0f;
            int lon = -1, loff = -1;   // Licht-An / Licht-Aus als Minute des Tages (-1 = kein Wechsel)
            phase_setpoints_t *csp = climate_setpoints(cc.phase);
            if (csp) {
                cvlo = csp->vpd_target - csp->vpd_deadband; cvhi = csp->vpd_target + csp->vpd_deadband;
                float tt = cc.is_day ? csp->temp_day : csp->temp_night;
                ctlo = tt - csp->temp_deadband; cthi = tt + csp->temp_deadband;
                float rt = cc.is_day ? csp->rh_day : csp->rh_night;
                crlo = rt - 5.0f; crhi = rt + 5.0f;
                if (csp->light_on_h > 0 && csp->light_on_h < 24) {
                    lon  = (csp->light_start_h % 24) * 60;
                    loff = (lon + csp->light_on_h * 60) % 1440;
                }
            }
            // Grow-Plan-Übersicht dieser Kammer (für Dashboard-Vorschau)
            int pday = 0, ptot = 0, pcur = 0, pnext = -1, pnin = 0;
            climate_plan_info(ci, &pday, &ptot, &pcur, &pnext, &pnin);
            bool plan_on = climate_plan_enabled(ci) && cgs != 0;
            // Plan-Schritte kompakt als [[phase,tage],...] für die Phasen-Zeitleiste
            char pst[96]; int pp = 0; pst[pp++] = '[';
            growplan_step_t gsteps[GROWPLAN_STEPS]; climate_get_growplan(ci, gsteps);
            for (int i = 0; i < GROWPLAN_STEPS; i++) {
                if (gsteps[i].days == 0) break;
                pp += snprintf(pst + pp, sizeof(pst) - pp, "%s[%d,%d]", i ? "," : "", gsteps[i].phase, gsteps[i].days);
            }
            pst[pp++] = ']'; pst[pp] = 0;
            n += snprintf(buf + n, sizeof(buf) - n,
                "%s{\"phase\":%d,\"auto\":%s,\"is_day\":%s,\"valid\":%s,\"temp\":%.1f,\"rh\":%.1f,"
                "\"vpd\":%.2f,\"ifan\":%d,\"src\":%d,\"alarm_temp\":%s,\"alarm_mold\":%s,"
                "\"alarm_light\":%s,\"lux\":%.0f,"
                "\"vlo\":%.2f,\"vhi\":%.2f,\"tlo\":%.1f,\"thi\":%.1f,\"rlo\":%.0f,\"rhi\":%.0f,"
                "\"lon\":%d,\"loff\":%d,\"gstart\":%lu,\"gdays\":%d,"
                "\"pl\":%s,\"pd\":%d,\"pt\":%d,\"pn\":%d,\"pni\":%d,\"af\":%s,\"afon\":%d,\"pst\":%s}",
                ci ? "," : "", cc.phase, climate_get_chamber_auto(ci) ? "true" : "false",
                cc.is_day ? "true" : "false",
                cc.valid ? "true" : "false", cc.temp, cc.rh, cc.vpd, cc.ifan_pct, src,
                cc.alarm_temp ? "true" : "false", cc.alarm_mold ? "true" : "false",
                cc.alarm_light ? "true" : "false", cc.light_lux,
                cvlo, cvhi, ctlo, cthi, crlo, crhi, lon, loff,
                (unsigned long)cgs, cgd,
                plan_on ? "true" : "false", pday, ptot, pnext, pnin,
                climate_af_mode(ci) ? "true" : "false", climate_af_light_on(ci), pst);
        }
        n += snprintf(buf + n, sizeof(buf) - n, "]}");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// liest ?key=...&key2=... Query-Parameter als int (gemeinsamer Helfer der Teilmodule)
int webui_qint(httpd_req_t *req, const char *key, int def)
{
    char q[160], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return def;
    if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return def;
    return atoi(v);
}

// Zentraler Schutz: erlaubt gültiges Session-Cookie (Browser-Login), ?key=<Token>
// oder Authorization: Bearer <Token> (Home Assistant/Skripte). Solange weder Passwort
// noch Token gesetzt sind, ist alles offen (kein Aussperren beim Erststart). Details in auth.c.
esp_err_t webui_guard(httpd_req_t *req)
{
    if (!auth_http_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_sendstr(req, "err: login/token required");
    }
    return ((esp_err_t (*)(httpd_req_t *))req->user_ctx)(req);
}

esp_err_t webui_register(httpd_handle_t srv)
{
    httpd_uri_t u_dash   = { .uri = "/",           .method = HTTP_GET, .handler = dash_get };
    httpd_uri_t u_status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get };
    httpd_uri_t u_i18n   = { .uri = "/i18n.js",    .method = HTTP_GET, .handler = i18n_get };
    httpd_register_uri_handler(srv, &u_dash);     // überschreibt OTA-Index → Dashboard ist Startseite
    httpd_register_uri_handler(srv, &u_status);
    httpd_register_uri_handler(srv, &u_i18n);
    webui_control_register(srv);
    webui_settings_register(srv);
    webui_diag_register(srv);
    ESP_LOGI(TAG, "Dashboard + Settings + API registriert");
    return ESP_OK;
}
