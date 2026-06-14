// webui_settings.c — WebUI-Teilmodul: Settings-Seite + /api/settings (GET/POST).
// Liest/schreibt Phasen-Sollwerte, globale Config, Aktor-/Kammer-Zuordnung und
// MQTT-Zugangsdaten als cJSON; Persistierung übernehmen die jeweiligen Module.
#include "webui_internal.h"
#include "settings_html.h"
#include "relays.h"
#include "relay_sched.h"
#include "dimmer.h"
#include "climate_control.h"
#include "watering.h"
#include "appcfg.h"
#include "auth.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ── Settings-Seite ──
static esp_err_t settings_get(httpd_req_t *req)
{
    if (auth_redirect_if_needed(req)) return ESP_OK;   // nicht eingeloggt → /login
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, SETTINGS_HTML, HTTPD_RESP_USE_STRLEN);
}

// GET /api/settings → alle Phasen-Sollwerte + MQTT als JSON
static esp_err_t api_settings_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *phases = cJSON_AddArrayToObject(root, "phases");
    for (int i = 0; i < PHASE_MAX; i++) {
        phase_setpoints_t *s = climate_setpoints(i);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "light_on_h",       s->light_on_h);
        cJSON_AddNumberToObject(p, "light_start_h",     s->light_start_h);
        cJSON_AddNumberToObject(p, "vpd_target",        s->vpd_target);
        cJSON_AddNumberToObject(p, "vpd_deadband",      s->vpd_deadband);
        cJSON_AddNumberToObject(p, "temp_day",          s->temp_day);
        cJSON_AddNumberToObject(p, "temp_night",        s->temp_night);
        cJSON_AddNumberToObject(p, "temp_deadband",     s->temp_deadband);
        cJSON_AddNumberToObject(p, "rh_day",            s->rh_day);
        cJSON_AddNumberToObject(p, "rh_night",          s->rh_night);
        cJSON_AddNumberToObject(p, "co2_target",        s->co2_target);
        cJSON_AddNumberToObject(p, "co2_only_daylight", s->co2_only_daylight);
        cJSON_AddNumberToObject(p, "light_pct",         s->light_pct);
        cJSON_AddNumberToObject(p, "ramp_min",          s->ramp_min);
        cJSON_AddNumberToObject(p, "temp_alarm",        s->temp_alarm);
        cJSON_AddNumberToObject(p, "rh_alarm",          s->rh_alarm);
        cJSON_AddNumberToObject(p, "temp_min_alarm",    s->temp_min_alarm);
        cJSON_AddNumberToObject(p, "co2_max_alarm",     s->co2_max_alarm);
        cJSON_AddNumberToObject(p, "dli_target",        climate_dli_target(i));  // separater NVS-Blob
        cJSON_AddNumberToObject(p, "light_mode",        climate_light_mode(i));  // 0=%, 1=PPFD
        cJSON_AddNumberToObject(p, "ppfd_target",       climate_ppfd_target(i)); // µmol/m²/s
        cJSON_AddItemToArray(phases, p);
    }

    // Aktor-Zuordnung: Funktion i → physische Steckdose (-1 = nicht belegt)
    cJSON *act = cJSON_AddArrayToObject(root, "actors");
    for (int i = 0; i < FN_MAX; i++)
        cJSON_AddItemToArray(act, cJSON_CreateNumber(relay_fn_get(i)));

    grow_global_t *g = climate_global();
    cJSON *gl = cJSON_AddObjectToObject(root, "global");
    cJSON_AddNumberToObject(gl, "buzzer_enable",   g->buzzer_enable);
    cJSON_AddNumberToObject(gl, "alarm_repeat_s",  g->alarm_repeat_s);
    cJSON_AddNumberToObject(gl, "fan_min_cycle_s", g->fan_min_cycle_s);
    cJSON_AddNumberToObject(gl, "lockout_dim",     g->lockout_dim);
    cJSON_AddNumberToObject(gl, "lockout_fan",     g->lockout_fan);
    cJSON_AddNumberToObject(gl, "sensor_alarm_en", g->sensor_alarm_en);
    cJSON_AddNumberToObject(gl, "fan_base",        g->fan_base);
    cJSON_AddNumberToObject(gl, "fan_max",         g->fan_max);
    cJSON_AddNumberToObject(gl, "light_fault_lux", g->light_fault_lux);
    cJSON_AddNumberToObject(gl, "water_max_min",   g->water_max_min);
    cJSON_AddNumberToObject(gl, "hist_save_min",   g->hist_save_min);

    // Bewässerungs-Automatik
    watering_cfg_t *w = watering_cfg();
    cJSON *wt = cJSON_AddObjectToObject(root, "watering");
    cJSON_AddNumberToObject(wt, "mode",          w->mode);
    cJSON_AddNumberToObject(wt, "only_day",      w->only_day);
    cJSON_AddNumberToObject(wt, "duration_s",    w->duration_s);
    cJSON_AddNumberToObject(wt, "interval_h",    w->interval_h);
    cJSON_AddNumberToObject(wt, "moist_low",     w->moist_low);
    cJSON_AddNumberToObject(wt, "min_pause_min", w->min_pause_min);

    // Steckdosen-Zeitpläne (Index = physische Steckdose)
    cJSON *sc = cJSON_AddArrayToObject(root, "sched");
    for (int i = 0; i < RLY_MAX; i++) {
        relay_sched_t *r = relay_sched(i);
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "mode", r->mode);
        cJSON_AddNumberToObject(e, "on",   r->on_min);
        cJSON_AddNumberToObject(e, "off",  r->off_min);
        cJSON_AddNumberToObject(e, "con",  r->cyc_on_min);
        cJSON_AddNumberToObject(e, "coff", r->cyc_off_min);
        cJSON_AddItemToArray(sc, e);
    }

    cJSON_AddNumberToObject(gl, "dli_floor_pct",   g->dli_floor_pct);
    cJSON_AddNumberToObject(gl, "water_alarm_en",  g->water_alarm_en);
    cJSON_AddNumberToObject(gl, "water_ph_min",    g->water_ph_min);
    cJSON_AddNumberToObject(gl, "water_ph_max",    g->water_ph_max);
    cJSON_AddNumberToObject(gl, "water_temp_max",  g->water_temp_max);
    cJSON_AddNumberToObject(gl, "water_temp_min",  g->water_temp_min);
    cJSON_AddNumberToObject(gl, "water_orp_min",   g->water_orp_min);
    cJSON_AddNumberToObject(gl, "ac_delay_min",    g->ac_delay_min);
    cJSON_AddNumberToObject(gl, "co2_baseline",    g->co2_baseline ? g->co2_baseline : 420);
    cJSON_AddNumberToObject(gl, "ac_chamber",      g->ac_chamber);
    cJSON_AddNumberToObject(gl, "par_chamber",     g->par_chamber);
    // Sensor-Kalibrier-Offsets je Slot (0/1 = Kammer A/B, 2 = Dachboden)
    cJSON *so = cJSON_AddObjectToObject(root, "soff");
    cJSON *sot = cJSON_AddArrayToObject(so, "t");
    cJSON *sor = cJSON_AddArrayToObject(so, "rh");
    for (int i = 0; i < N_SENS; i++) {
        cJSON_AddItemToArray(sot, cJSON_CreateNumber(climate_temp_offset(i)));
        cJSON_AddItemToArray(sor, cJSON_CreateNumber(climate_rh_offset(i)));
    }
    cJSON *dim = cJSON_AddObjectToObject(root, "dimmer");
    cJSON_AddBoolToObject(dim, "inv0", dimmer_get_invert(DIM_LIGHT1));
    cJSON_AddBoolToObject(dim, "inv1", dimmer_get_invert(DIM_LIGHT2));
    cJSON_AddNumberToObject(dim, "cal0", dimmer_get_cal(DIM_LIGHT1));
    cJSON_AddNumberToObject(dim, "cal1", dimmer_get_cal(DIM_LIGHT2));

    // 2-Kammer-Zuordnung (Steckdose→Kammer+Rolle, Dimmer/iFan-Kammer, Profil+Sensor je Kammer)
    cJSON *asg = cJSON_AddObjectToObject(root, "asg");
    cJSON *relch = cJSON_AddArrayToObject(asg, "relch");
    cJSON *relrole = cJSON_AddArrayToObject(asg, "relrole");
    for (int i = 0; i < RLY_MAX; i++) {
        cJSON_AddItemToArray(relch,   cJSON_CreateNumber(relay_chamber(i)));
        cJSON_AddItemToArray(relrole, cJSON_CreateNumber(relay_role(i)));
    }
    cJSON *dimch = cJSON_AddArrayToObject(asg, "dimch");
    cJSON_AddItemToArray(dimch, cJSON_CreateNumber(dim_chamber(0)));
    cJSON_AddItemToArray(dimch, cJSON_CreateNumber(dim_chamber(1)));
    cJSON_AddNumberToObject(asg, "ifanch", ifan_chamber());
    cJSON *cph  = cJSON_AddArrayToObject(asg, "phase");
    cJSON *caut = cJSON_AddArrayToObject(asg, "cauto");
    cJSON *csrc = cJSON_AddArrayToObject(asg, "csrc");
    cJSON *cmac = cJSON_AddArrayToObject(asg, "cmac");
    for (int ch = 0; ch < N_CHAMBERS; ch++) {
        cJSON_AddItemToArray(cph,  cJSON_CreateNumber(climate_get_phase(ch)));
        cJSON_AddItemToArray(caut, cJSON_CreateBool(climate_get_chamber_auto(ch)));
    }
    // csrc/cmac: Index 0/1 = Kammer A/B, Index CSENS_ATTIC = Dachboden
    for (int si = 0; si < N_SENS; si++) {
        sensor_src_t sr = SRC_TH_SENSOR; uint8_t mac[6] = {0};
        climate_get_sensor(si, &sr, mac);
        cJSON_AddItemToArray(csrc, cJSON_CreateNumber(sr));
        char ms[13];
        snprintf(ms, sizeof(ms), "%02X%02X%02X%02X%02X%02X",
                 mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);  // Anzeige-Reihenfolge
        cJSON_AddItemToArray(cmac, cJSON_CreateString(ms));
    }

    // ── Grow-Plan je Kammer ──
    cJSON *gp = cJSON_AddObjectToObject(root, "growplan");
    cJSON *gpen = cJSON_AddArrayToObject(gp, "en");
    cJSON *gpst = cJSON_AddArrayToObject(gp, "start");
    cJSON *gpdy = cJSON_AddArrayToObject(gp, "days");
    cJSON *gpday = cJSON_AddArrayToObject(gp, "today");   // aktueller Plan-Tag (Anzeige)
    cJSON *gpaf  = cJSON_AddArrayToObject(gp, "af");      // Autoflower-Modus je Kammer
    cJSON *gpafon = cJSON_AddArrayToObject(gp, "afon");   // Licht-Stunden im Autoflower-Modus
    cJSON *gpafst = cJSON_AddArrayToObject(gp, "afst");   // Start-Stunde
    cJSON *gpsteps = cJSON_AddArrayToObject(gp, "steps");
    for (int ch = 0; ch < N_CHAMBERS; ch++) {
        cJSON_AddItemToArray(gpen, cJSON_CreateBool(climate_plan_enabled(ch)));
        uint32_t st = 0; uint16_t dy = 0; climate_get_grow(ch, &st, &dy);
        cJSON_AddItemToArray(gpst, cJSON_CreateNumber(st));
        cJSON_AddItemToArray(gpdy, cJSON_CreateNumber(dy));
        cJSON_AddItemToArray(gpday, cJSON_CreateNumber(climate_plan_day(ch)));
        cJSON_AddItemToArray(gpaf,  cJSON_CreateBool(climate_af_mode(ch)));
        cJSON_AddItemToArray(gpafon, cJSON_CreateNumber(climate_af_light_on(ch)));
        cJSON_AddItemToArray(gpafst, cJSON_CreateNumber(climate_af_light_start(ch)));
        growplan_step_t steps[GROWPLAN_STEPS]; climate_get_growplan(ch, steps);
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < GROWPLAN_STEPS; i++) {
            cJSON *s = cJSON_CreateObject();
            cJSON_AddNumberToObject(s, "p", steps[i].phase);
            cJSON_AddNumberToObject(s, "d", steps[i].days);
            cJSON_AddItemToArray(arr, s);
        }
        cJSON_AddItemToArray(gpsteps, arr);
    }

    cJSON *mq = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddStringToObject(mq, "uri",  appcfg_mqtt_uri());
    cJSON_AddStringToObject(mq, "user", appcfg_mqtt_user());
    // Secrets are NEVER echoed back; the UI shows only whether one is set. Empty field on
    // POST = keep current value, so masking does not wipe the stored secret.
    cJSON_AddStringToObject(mq, "pass", "");
    cJSON_AddBoolToObject(mq,  "pass_set",  appcfg_mqtt_pass()[0] != 0);
    cJSON_AddStringToObject(mq, "token", "");
    cJSON_AddBoolToObject(mq,  "token_set", appcfg_api_token()[0] != 0);
    cJSON_AddStringToObject(mq, "host", appcfg_hostname());     // mDNS (<host>.local)
    cJSON_AddStringToObject(mq, "tz",   appcfg_tz());           // Zeitzone (POSIX-TZ)
    cJSON_AddStringToObject(mq, "appw", appcfg_ap_pass());      // Hotspot-PW (read-only Anzeige)
    cJSON_AddNumberToObject(mq, "protect", appcfg_api_protect() ? 1 : 0);  // API-Schutz Steuer-API

    // WebUI-Login (Benutzer + ob ein Passwort gesetzt ist; Hash wird nie ausgegeben)
    cJSON *au = cJSON_AddObjectToObject(root, "auth");
    cJSON_AddStringToObject(au, "user", auth_username());
    cJSON_AddBoolToObject(au,   "pass_set", auth_has_password());

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    cJSON_Delete(root);
    return e;
}

// POST /api/settings ← JSON, schreibt Sollwerte (NVS) + MQTT (NVS)
static esp_err_t api_settings_post(httpd_req_t *req)
{
    char buf[4096];
    int total = 0, r;
    while (total < (int)sizeof(buf) - 1 &&
           (r = httpd_req_recv(req, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += r;
    if (total <= 0) return ESP_FAIL;
    buf[total] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON?"); return ESP_FAIL; }

    cJSON *phases = cJSON_GetObjectItem(root, "phases");
    if (cJSON_IsArray(phases)) {
        int i = 0;
        cJSON *p;
        cJSON_ArrayForEach(p, phases) {
            if (i >= PHASE_MAX) break;
            phase_setpoints_t *s = climate_setpoints(i);
            #define GETI(k,field) { cJSON *v=cJSON_GetObjectItem(p,k); if(cJSON_IsNumber(v)) s->field=(typeof(s->field))v->valuedouble; }
            GETI("light_on_h", light_on_h);   GETI("light_start_h", light_start_h);
            GETI("vpd_target", vpd_target);   GETI("vpd_deadband", vpd_deadband);
            GETI("temp_day", temp_day);       GETI("temp_night", temp_night);
            GETI("temp_deadband", temp_deadband);
            GETI("rh_day", rh_day);           GETI("rh_night", rh_night);
            GETI("co2_target", co2_target);   GETI("co2_only_daylight", co2_only_daylight);
            GETI("light_pct", light_pct);     GETI("ramp_min", ramp_min);
            GETI("temp_alarm", temp_alarm);   GETI("rh_alarm", rh_alarm);
            GETI("temp_min_alarm", temp_min_alarm); GETI("co2_max_alarm", co2_max_alarm);
            #undef GETI
            // DLI-Ziel + PPFD-Lichtregelung liegen NICHT im Setpoint-Struct (eigene NVS-Blobs)
            cJSON *dv = cJSON_GetObjectItem(p, "dli_target");
            if (cJSON_IsNumber(dv)) climate_set_dli_target(i, (float)dv->valuedouble);
            cJSON *lm = cJSON_GetObjectItem(p, "light_mode");
            if (cJSON_IsNumber(lm)) climate_set_light_mode(i, (uint8_t)lm->valuedouble);
            cJSON *pt = cJSON_GetObjectItem(p, "ppfd_target");
            if (cJSON_IsNumber(pt)) climate_set_ppfd_target(i, (uint16_t)pt->valuedouble);
            i++;
        }
        climate_save_setpoints();
        climate_save_dli_targets();
        climate_save_light_ppfd();
    }

    cJSON *act = cJSON_GetObjectItem(root, "actors");
    if (cJSON_IsArray(act)) {
        int i = 0; cJSON *v;
        cJSON_ArrayForEach(v, act) {
            if (i >= FN_MAX) break;
            if (cJSON_IsNumber(v)) relay_fn_set((func_id_t)i, (int8_t)v->valuedouble);
            i++;
        }
    }

    cJSON *gl = cJSON_GetObjectItem(root, "global");
    if (gl) {
        grow_global_t *g = climate_global();
        #define GETG(k,field) { cJSON *v=cJSON_GetObjectItem(gl,k); if(cJSON_IsNumber(v)) g->field=(typeof(g->field))v->valuedouble; }
        GETG("buzzer_enable", buzzer_enable);     GETG("alarm_repeat_s", alarm_repeat_s);
        GETG("fan_min_cycle_s", fan_min_cycle_s); GETG("lockout_dim", lockout_dim);
        GETG("lockout_fan", lockout_fan);         GETG("sensor_alarm_en", sensor_alarm_en);
        GETG("fan_base", fan_base);               GETG("fan_max", fan_max);
        GETG("light_fault_lux", light_fault_lux);
        GETG("water_max_min", water_max_min);
        GETG("hist_save_min", hist_save_min);
        GETG("dli_floor_pct", dli_floor_pct);
        GETG("water_alarm_en", water_alarm_en);
        GETG("water_ph_min", water_ph_min);   GETG("water_ph_max", water_ph_max);
        GETG("water_temp_max", water_temp_max); GETG("water_temp_min", water_temp_min);
        GETG("water_orp_min", water_orp_min);
        GETG("ac_delay_min", ac_delay_min);
        GETG("co2_baseline", co2_baseline);
        GETG("ac_chamber", ac_chamber);
        GETG("par_chamber", par_chamber);
        #undef GETG
        climate_save_global();
    }

    cJSON *wt = cJSON_GetObjectItem(root, "watering");
    if (wt) {
        watering_cfg_t *w = watering_cfg();
        #define GETW(k,field,maxv) { cJSON *v=cJSON_GetObjectItem(wt,k); \
            if(cJSON_IsNumber(v)){ double x=v->valuedouble; if(x<0)x=0; if(x>(maxv))x=(maxv); \
            w->field=(typeof(w->field))x; } }
        GETW("mode",          mode,          2);
        GETW("only_day",      only_day,      1);
        GETW("duration_s",    duration_s,    3600);
        GETW("interval_h",    interval_h,    720);
        GETW("moist_low",     moist_low,     100);
        GETW("min_pause_min", min_pause_min, 1440);
        #undef GETW
        watering_save_cfg();
    }

    cJSON *sc = cJSON_GetObjectItem(root, "sched");
    if (cJSON_IsArray(sc)) {
        int i = 0; cJSON *e;
        cJSON_ArrayForEach(e, sc) {
            if (i >= RLY_MAX) break;
            relay_sched_t *r = relay_sched(i);
            #define GETS(k,field,maxv) { cJSON *v=cJSON_GetObjectItem(e,k); \
                if(cJSON_IsNumber(v)){ double x=v->valuedouble; if(x<0)x=0; if(x>(maxv))x=(maxv); \
                r->field=(typeof(r->field))x; } }
            GETS("mode", mode,        2);
            GETS("on",   on_min,      1439);
            GETS("off",  off_min,     1439);
            GETS("con",  cyc_on_min,  1440);
            GETS("coff", cyc_off_min, 1440);
            #undef GETS
            i++;
        }
        relay_sched_save();
    }

    cJSON *dim = cJSON_GetObjectItem(root, "dimmer");
    if (dim) {
        cJSON *i0 = cJSON_GetObjectItem(dim, "inv0");
        cJSON *i1 = cJSON_GetObjectItem(dim, "inv1");
        if (cJSON_IsBool(i0)) dimmer_set_invert(DIM_LIGHT1, cJSON_IsTrue(i0));
        if (cJSON_IsBool(i1)) dimmer_set_invert(DIM_LIGHT2, cJSON_IsTrue(i1));
        cJSON *c0 = cJSON_GetObjectItem(dim, "cal0");
        cJSON *c1 = cJSON_GetObjectItem(dim, "cal1");
        if (cJSON_IsNumber(c0)) dimmer_set_cal(DIM_LIGHT1, (uint8_t)c0->valuedouble);
        if (cJSON_IsNumber(c1)) dimmer_set_cal(DIM_LIGHT2, (uint8_t)c1->valuedouble);
    }

    cJSON *mq = cJSON_GetObjectItem(root, "mqtt");
    if (mq) {
        const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(mq, "uri"));
        const char *us  = cJSON_GetStringValue(cJSON_GetObjectItem(mq, "user"));
        const char *pw  = cJSON_GetStringValue(cJSON_GetObjectItem(mq, "pass"));
        // empty password field = keep current (it is never echoed back to the UI)
        appcfg_set_mqtt(uri, us, (pw && pw[0]) ? pw : NULL);
        // API token: explicit clear flag wins; empty field = keep current
        cJSON *tkc = cJSON_GetObjectItem(mq, "token_clear");
        cJSON *tk  = cJSON_GetObjectItem(mq, "token");
        if (tkc && (cJSON_IsTrue(tkc) || (cJSON_IsNumber(tkc) && tkc->valuedouble != 0)))
            appcfg_set_api_token("");
        else if (cJSON_IsString(tk) && cJSON_GetStringValue(tk)[0])
            appcfg_set_api_token(cJSON_GetStringValue(tk));
        cJSON *ho = cJSON_GetObjectItem(mq, "host");
        if (cJSON_IsString(ho) && cJSON_GetStringValue(ho)[0]) appcfg_set_hostname(cJSON_GetStringValue(ho));
        cJSON *tz = cJSON_GetObjectItem(mq, "tz");
        if (cJSON_IsString(tz) && cJSON_GetStringValue(tz)[0]) appcfg_set_tz(cJSON_GetStringValue(tz));
        cJSON *pr = cJSON_GetObjectItem(mq, "protect");
        if (cJSON_IsNumber(pr)) appcfg_set_api_protect(pr->valuedouble != 0);
    }

    // ── WebUI-Login (Benutzer/Passwort) ──
    cJSON *au = cJSON_GetObjectItem(root, "auth");
    if (au) {
        const char *nu = cJSON_GetStringValue(cJSON_GetObjectItem(au, "user"));
        const char *np = cJSON_GetStringValue(cJSON_GetObjectItem(au, "pass"));
        cJSON *clr = cJSON_GetObjectItem(au, "clear");
        bool do_clear = clr && (cJSON_IsTrue(clr) || (cJSON_IsNumber(clr) && clr->valuedouble != 0));
        if (do_clear)            auth_set_credentials((nu && nu[0]) ? nu : NULL, "");   // login deaktivieren
        else if (np && np[0])    auth_set_credentials((nu && nu[0]) ? nu : NULL, np);   // Passwort setzen/ändern
        else if (nu && nu[0])    auth_set_credentials(nu, NULL);                        // nur Benutzername
    }

    // 2-Kammer-Zuordnung
    cJSON *asg = cJSON_GetObjectItem(root, "asg");
    if (asg) {
        cJSON *relch = cJSON_GetObjectItem(asg, "relch");
        cJSON *relrole = cJSON_GetObjectItem(asg, "relrole");
        if (cJSON_IsArray(relch) && cJSON_IsArray(relrole)) {
            for (int i = 0; i < RLY_MAX; i++) {
                cJSON *c = cJSON_GetArrayItem(relch, i), *rr = cJSON_GetArrayItem(relrole, i);
                if (cJSON_IsNumber(c) && cJSON_IsNumber(rr))
                    relay_assign((relay_id_t)i, (int8_t)c->valuedouble, (func_id_t)rr->valuedouble);
            }
        }
        cJSON *dimch = cJSON_GetObjectItem(asg, "dimch");
        if (cJSON_IsArray(dimch))
            for (int i = 0; i < 2; i++) {
                cJSON *c = cJSON_GetArrayItem(dimch, i);
                if (cJSON_IsNumber(c)) dim_set_chamber(i, (int8_t)c->valuedouble);
            }
        cJSON *ifc = cJSON_GetObjectItem(asg, "ifanch");
        if (cJSON_IsNumber(ifc)) ifan_set_chamber((int8_t)ifc->valuedouble);
        cJSON *cph  = cJSON_GetObjectItem(asg, "phase");
        cJSON *csrc = cJSON_GetObjectItem(asg, "csrc");
        cJSON *cmac = cJSON_GetObjectItem(asg, "cmac");
        for (int ch = 0; ch < N_CHAMBERS; ch++) {
            cJSON *ph = cJSON_GetArrayItem(cph, ch);
            if (cJSON_IsNumber(ph)) climate_set_phase(ch, (grow_phase_t)ph->valuedouble);
        }
        // Sensor-Quellen: 0/1 = Kammer A/B, CSENS_ATTIC = Dachboden
        for (int si = 0; si < N_SENS; si++) {
            cJSON *sr = cJSON_GetArrayItem(csrc, si);
            cJSON *mc = cJSON_GetArrayItem(cmac, si);
            if (!cJSON_IsNumber(sr)) continue;
            int src = (int)sr->valuedouble;
            uint8_t mac[6] = {0}; bool hasmac = false;
            const char *ms = cJSON_GetStringValue(mc);
            if (ms && strlen(ms) >= 12) {
                for (int i = 0; i < 6; i++) {
                    char b[3] = { ms[i*2], ms[i*2+1], 0 };
                    mac[5 - i] = (uint8_t)strtol(b, NULL, 16);  // Anzeige→addr.val
                }
                hasmac = true;
            }
            climate_set_sensor(si, (sensor_src_t)src, hasmac ? mac : NULL);
        }
    }
    // ── Grow-Plan ──
    cJSON *gp = cJSON_GetObjectItem(root, "growplan");
    if (gp) {
        cJSON *gpen = cJSON_GetObjectItem(gp, "en");
        cJSON *gpst = cJSON_GetObjectItem(gp, "start");
        cJSON *gpdy = cJSON_GetObjectItem(gp, "days");
        cJSON *gpaf = cJSON_GetObjectItem(gp, "af");
        cJSON *gpafon = cJSON_GetObjectItem(gp, "afon");
        cJSON *gpafst = cJSON_GetObjectItem(gp, "afst");
        cJSON *gpsteps = cJSON_GetObjectItem(gp, "steps");
        for (int ch = 0; ch < N_CHAMBERS; ch++) {
            cJSON *e = cJSON_GetArrayItem(gpen, ch);
            if (e) climate_set_plan_enabled(ch, cJSON_IsTrue(e) || (cJSON_IsNumber(e) && e->valuedouble != 0));
            cJSON *st = cJSON_GetArrayItem(gpst, ch);
            cJSON *dy = cJSON_GetArrayItem(gpdy, ch);
            if (cJSON_IsNumber(st))
                climate_set_grow(ch, (uint32_t)st->valuedouble, cJSON_IsNumber(dy) ? (uint16_t)dy->valuedouble : 0);
            cJSON *af = cJSON_GetArrayItem(gpaf, ch);
            cJSON *afo = cJSON_GetArrayItem(gpafon, ch);
            cJSON *afs = cJSON_GetArrayItem(gpafst, ch);
            uint8_t afm = (af && (cJSON_IsTrue(af) || (cJSON_IsNumber(af) && af->valuedouble != 0))) ? 1 : 0;
            climate_set_af(ch, afm,
                           cJSON_IsNumber(afo) ? (uint8_t)afo->valuedouble : climate_af_light_on(ch),
                           cJSON_IsNumber(afs) ? (uint8_t)afs->valuedouble : climate_af_light_start(ch));
            cJSON *arr = cJSON_GetArrayItem(gpsteps, ch);
            if (cJSON_IsArray(arr)) {
                growplan_step_t steps[GROWPLAN_STEPS]; memset(steps, 0, sizeof(steps));
                int i = 0; cJSON *s;
                cJSON_ArrayForEach(s, arr) {
                    if (i >= GROWPLAN_STEPS) break;
                    cJSON *p = cJSON_GetObjectItem(s, "p");
                    cJSON *d = cJSON_GetObjectItem(s, "d");
                    if (cJSON_IsNumber(p)) steps[i].phase = (uint8_t)p->valuedouble;
                    if (cJSON_IsNumber(d)) steps[i].days  = (uint16_t)d->valuedouble;
                    i++;
                }
                climate_set_growplan(ch, steps);
            }
        }
        climate_save_growplan();
    }
    // ── Sensor-Kalibrier-Offsets ──
    cJSON *so = cJSON_GetObjectItem(root, "soff");
    if (so) {
        cJSON *sot = cJSON_GetObjectItem(so, "t");
        cJSON *sor = cJSON_GetObjectItem(so, "rh");
        for (int i = 0; i < N_SENS; i++) {
            cJSON *t = cJSON_GetArrayItem(sot, i);
            cJSON *r = cJSON_GetArrayItem(sor, i);
            climate_set_offsets(i,
                cJSON_IsNumber(t) ? (float)t->valuedouble : climate_temp_offset(i),
                cJSON_IsNumber(r) ? (float)r->valuedouble : climate_rh_offset(i));
        }
        climate_save_offsets();
    }
    cJSON_Delete(root);
    return httpd_resp_sendstr(req, "ok");
}

// /api/settings ist in BEIDE Richtungen geschützt (GET liefert MQTT-Pass + Token!);
// nur die statische Settings-Seite selbst bleibt offen.
static const httpd_uri_t k_uris[] = {
    { .uri = "/settings",     .method = HTTP_GET,  .handler = settings_get },
    { .uri = "/api/settings", .method = HTTP_GET,  .handler = webui_guard, .user_ctx = (void *)api_settings_get },
    { .uri = "/api/settings", .method = HTTP_POST, .handler = webui_guard, .user_ctx = (void *)api_settings_post },
};

esp_err_t webui_settings_register(httpd_handle_t srv)
{
    for (size_t i = 0; i < sizeof(k_uris) / sizeof(k_uris[0]); i++)
        httpd_register_uri_handler(srv, &k_uris[i]);
    return ESP_OK;
}
