// climate_control.c — Klima-Teilmodul: Cannabis-Grow-Regelung + Regel-Task.
// Liest die Konfiguration (Sollwerte/Phasen/Zuordnung/Overrides) ausschließlich
// über die öffentliche API aus climate_config.c; besitzt selbst den Live-Zustand
// (s_status/s_cstate), die Alarm-Quittierung und den Regel-Task — der EINZIGE
// Modbus-Bus-Nutzer (poll) + State-Schreiber.
#include "climate_control.h"
#include "climate_internal.h"
#include "devices.h"
#include "bl0940.h"
#include "history.h"
#include "relays.h"
#include "relay_sched.h"
#include "dimmer.h"
#include "buzzer.h"
#include "ble_sensors.h"
#include "watering.h"
#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "nvsutil.h"
#include <time.h>
#include <string.h>

static const char *TAG = "climate";

static chamber_state_t s_cstate[N_CHAMBERS];   // Live-Zustand je Kammer

static climate_status_t s_status;              // Aggregat (für LEDs/MQTT/Dashboard-Status)
static bool s_ac_throttle = false;             // AC-Kammer fordert AC an → iFan-Eigner drosseln
                                               // (entkoppelt Rechen-Kammer von Stell-Kammer)
static bool s_ifan_force_max = false;          // Übertemperatur/Schimmel der iFan-Eigner-Kammer →
                                               // Notfall-Vollabluft hat Vorrang vor dem manuellen
                                               // iFan-Override (Sicherheit vor Komfort)

// ── Alarm-Ereignisprotokoll (RAM-Ringpuffer) ──
#define ALOG_N 24
static alarm_evt_t s_alog[ALOG_N];
static uint8_t s_alog_head = 0, s_alog_count = 0, s_alarm_prev = 0;
static void alog_push(uint8_t type, uint8_t on, uint32_t ts)
{
    s_alog[s_alog_head] = (alarm_evt_t){ ts, type, on };
    s_alog_head = (s_alog_head + 1) % ALOG_N;
    if (s_alog_count < ALOG_N) s_alog_count++;
}
int climate_alarmlog(alarm_evt_t *out, int max)
{
    int n = (s_alog_count < max) ? s_alog_count : max;
    for (int i = 0; i < n; i++)
        out[i] = s_alog[(s_alog_head - 1 - i + ALOG_N * 2) % ALOG_N];   // neueste zuerst
    return n;
}
static volatile bool s_ack_req = false;        // BT-Quittierung angefordert (im Task verarbeitet)

// Quittier-Status PRO Alarmtyp, in NVS persistiert (überlebt Reboot/Stromausfall):
// ein quittierter Alarm bleibt stumm, bis er normalisiert war.
static bool s_ack_temp = false, s_ack_mold = false, s_ack_co2 = false, s_ack_sens = false;
static bool s_ack_light = false, s_ack_water = false;

static void ack_save(void)
{
    uint8_t m = (s_ack_temp ? 1 : 0) | (s_ack_mold ? 2 : 0)
              | (s_ack_co2 ? 4 : 0) | (s_ack_sens ? 8 : 0) | (s_ack_light ? 16 : 0)
              | (s_ack_water ? 32 : 0);
    nvsu_set_u8("grow", "ackmask", m);
}

static void ack_load(void)
{
    uint8_t m = 0;
    nvsu_get_u8("grow", "ackmask", &m);
    s_ack_temp = m & 1; s_ack_mold = m & 2; s_ack_co2 = m & 4; s_ack_sens = m & 8;
    s_ack_light = m & 16; s_ack_water = m & 32;
}

// Quittieren: stummschalten. Der Regel-Task merkt sich (persistent), WELCHE Alarme
// gerade aktiv waren, und lässt genau die stumm, bis sie normalisiert waren — ein
// anderer/neuer Alarmtyp piept sofort.
void climate_alarm_ack(void) { s_ack_req = true; }

static float s_dli_accum = 0;     // mol/m² seit Mitternacht
static int   s_last_yday = -1;

esp_err_t climate_init(void)
{
    climate_config_init();         // Phasen/Sollwerte/Global/Grow/Overrides aus NVS
    ack_load();                    // Alarm-Quittierungen (überleben Reboot)
    watering_init();               // Bewässerungs-Config + letzter Gieß-Start aus NVS
    // Verlauf + DLI-Tagessumme aus dem Flash wiederherstellen (falls gesichert).
    // Der Zeit-Guard in regulate() verhindert, dass die ungültige Boot-Uhr (1970)
    // die wiederhergestellte Tagessumme sofort wieder nullt.
    float dli; int yday;
    if (history_restore(&dli, &yday)) { s_dli_accum = dli; s_last_yday = yday; }
    memset(&s_status, 0, sizeof(s_status));
    memset(s_cstate, 0, sizeof(s_cstate));
    s_status.phase = climate_get_phase(0);
    ESP_LOGI(TAG, "Klimaregelung init, Kammer A: %s, Kammer B: %s",
             climate_phase_name(climate_get_phase(0)), climate_phase_name(climate_get_phase(1)));
    return ESP_OK;
}

void climate_chamber(int chamber, chamber_state_t *out)
{
    if (chamber >= 0 && chamber < N_CHAMBERS) *out = s_cstate[chamber];
    else memset(out, 0, sizeof(*out));
}

void climate_get_status(climate_status_t *o) { *o = s_status; }

// Zeitschaltuhr Licht: AN ab light_start_h für light_on_h Stunden,
// mit korrektem Mitternachts-Überlauf (z. B. Start 18:00, 12h → bis 06:00).
// PHASE_DRY (light_on_h==0): nie Licht.
static bool is_light_on(const phase_setpoints_t *s, const struct tm *now)
{
    if (s->light_on_h == 0) return false;
    if (s->light_on_h >= 24) return true;
    int h = now->tm_hour;
    int start = s->light_start_h % 24;
    int end   = (start + s->light_on_h) % 24;
    if (start < end) return (h >= start && h < end);
    return (h >= start || h < end);   // über Mitternacht
}

// Helligkeit mit Sonnenauf-/untergang-Rampe. Liefert 0..light_pct.
// Am Anfang der Lichtphase über ramp_min hochrampen, am Ende runter.
static uint8_t light_brightness(const phase_setpoints_t *s, const struct tm *now)
{
    if (!is_light_on(s, now)) return 0;
    if (s->ramp_min == 0) return s->light_pct;   // Rampe aus → hart

    int now_min   = now->tm_hour * 60 + now->tm_min;
    int start_min = (s->light_start_h % 24) * 60;
    int total_min = s->light_on_h * 60;
    int elapsed   = (now_min - start_min + 1440) % 1440;  // Minuten seit Licht-AN
    int remaining = total_min - elapsed;

    if (elapsed < s->ramp_min)               // Sonnenaufgang
        return (uint8_t)(s->light_pct * elapsed / s->ramp_min);
    if (remaining < s->ramp_min)             // Sonnenuntergang
        return (uint8_t)(s->light_pct * remaining / s->ramp_min);
    return s->light_pct;                      // Plateau
}

// ── Kammer-bezogene Aktor-Helfer ──
// Schalten erfolgt über die physische Steckdose mit (Kammer, Rolle); nicht belegt → no-op.
static void ch_relay(int chamber, func_id_t role, bool on)
{
    int p = relay_find(chamber, role);
    if (p >= 0) relay_set_auto((relay_id_t)p, on);
}

// Dimmer dieser Kammer setzen (Dimmer 0/1 sind je einer Kammer zugeordnet).
static void ch_dim(int chamber, uint8_t br)
{
    if (dim_chamber(0) == chamber) dimmer_set(DIM_LIGHT1, br);
    if (dim_chamber(1) == chamber) dimmer_set(DIM_LIGHT2, br);
}

// Klimasensor-Werte der Kammer beschaffen (je nach zugeordneter Quelle).
static bool chamber_climate(int chamber, const sensor_data_t *d, float *t, float *rh)
{
    sensor_src_t src = SRC_TH_SENSOR; uint8_t mac[6] = {0};
    climate_get_sensor(chamber, &src, mac);
    switch (src) {
    case SRC_TH_SENSOR:
        if (d->th3in1_valid) { *t = d->temperature_c; *rh = d->humidity_pct; return true; }
        return false;
    case SRC_TH_FAN:
        if (d->attic_valid)  { *t = d->attic_temp_c; *rh = d->attic_humidity_pct; return true; }
        return false;
    case SRC_BLE:
        return ble_get_th(mac, t, rh);
    }
    return false;
}

// Dachboden-/Quellluft-Werte (Free-Cooling-Referenz) je nach konfigurierter Quelle
// in die sensor_data-Attic-Felder schreiben. Default SRC_TH_FAN = Fan-Bus (devices_poll
// hat das schon gefüllt → nichts tun). Sonst aus BLE (TP357) bzw. Sensor-Bus übernehmen.
static void apply_attic_source(sensor_data_t *d)
{
    // Dachboden-REFERENZ in d->ref_* schreiben (NICHT d->attic_* — das ist der Fan-Bus-
    // Rohwert, den Kammern mit SRC_TH_FAN lesen). So zeigen Fan-Bus-Kammer und Dachboden
    // unabhängige Werte, auch wenn der Dachboden auf BLE/Sensor-Bus steht.
    sensor_src_t src = SRC_TH_FAN; uint8_t mac[6] = {0};
    climate_get_sensor(CSENS_ATTIC, &src, mac);
    float t = 0, rh = 0; bool ok = false;
    switch (src) {
    case SRC_TH_FAN:    ok = d->attic_valid; t = d->attic_temp_c; rh = d->attic_humidity_pct; break;
    case SRC_BLE:       ok = ble_get_th(mac, &t, &rh); break;
    case SRC_TH_SENSOR: if (d->th3in1_valid) { t = d->temperature_c; rh = d->humidity_pct; ok = true; } break;
    }
    d->ref_valid = ok;
    if (ok) {
        // Dachboden-Sensor-Offset (Slot CSENS_ATTIC) — fairer Vergleich mit den Kammer-Sensoren.
        t  += climate_temp_offset(CSENS_ATTIC);
        rh += climate_rh_offset(CSENS_ATTIC);
        if (rh < 0.0f) rh = 0.0f; if (rh > 100.0f) rh = 100.0f;
        d->ref_temp_c = t; d->ref_humidity_pct = rh;
    }
}

// ── Lichtaus-Vorwarnung: 15s-Dauerton 5 min bevor in einer Kammer das Licht ausgeht ──
// (z.B. um vor Beginn der Dunkelphase noch in der Kammer fertig zu werden.) Einmal je
// Kammer & Tag. Schlüssel = yday*1440 + Warn-Minute, verhindert Mehrfach-Auslösen.
static int s_warned[N_CHAMBERS] = { -1, -1 };

// Zeit-Guard: letzter bekannter Tag/Nacht-Zustand je Kammer (gehalten, wenn die Systemzeit
// ungültig wird), und ob es je eine gültige Zeit gab.
static bool s_last_day[N_CHAMBERS]  = { false, false };
static bool s_had_time[N_CHAMBERS]  = { false, false };

static void check_light_warning(const struct tm *now)
{
    int now_min  = now->tm_hour * 60 + now->tm_min;
    for (int ch = 0; ch < N_CHAMBERS; ch++) {
        grow_phase_t p = climate_get_phase(ch);
        if (p == PHASE_OFF) continue;
        const phase_setpoints_t *s = climate_setpoints(p);
        if (s->light_on_h == 0 || s->light_on_h >= 24) continue;   // kein Tag/Nacht-Wechsel
        int off_min  = ((s->light_start_h % 24) * 60 + s->light_on_h * 60) % 1440;
        int warn_min = (off_min - 5 + 1440) % 1440;                 // 5 min vorher
        if (now_min == warn_min) {
            int key = now->tm_yday * 1440 + warn_min;
            if (s_warned[ch] != key) {
                s_warned[ch] = key;
                buzzer_tone_ms(2700, 15000);                        // 15s Dauerton
                ESP_LOGI(TAG, "Kammer %c: Licht aus in 5 min → Vorwarnung", 'A' + ch);
            }
        }
    }
}

// Regelung EINER Kammer. Aktoren via (Kammer,Rolle); der RS-485-Bus (stufenloser
// iFan + MarsHydro-Humidifier) gehört genau einer Kammer (= ifan_chamber()).
static void regulate_chamber(int ch, const sensor_data_t *d, const struct tm *now)
{
    chamber_state_t *cs = &s_cstate[ch];
    const grow_global_t *g = climate_global();
    bool rs485_owner = (ifan_chamber() == ch);
    grow_phase_t phase = climate_get_phase(ch);
    cs->phase = phase;

    // ── Automatik für diese Kammer AUS: nur Messwerte aktualisieren, KEINE Aktoren ──
    // (Lichter/Relais/iFan/Befeuchter bleiben auf manuell/letztem Wert.)
    if (!climate_get_chamber_auto(ch)) {
        float t, rh;
        cs->valid = chamber_climate(ch, d, &t, &rh);
        if (cs->valid) { cs->temp = t; cs->rh = rh; cs->vpd = vpd_kpa(t, rh); }
        return;
    }

    // ── Profil „Aus": keine Automatik/Aktoren, aber Sensor TROTZDEM lesen + anzeigen ──
    if (phase == PHASE_OFF) {
        cs->is_day = false;
        cs->alarm_temp = cs->alarm_mold = cs->alarm_light = false;
        cs->light_lux = 0;
        float t, rh;
        cs->valid = chamber_climate(ch, d, &t, &rh);   // Messwert zeigen, auch wenn Automatik aus
        if (cs->valid) { cs->temp = t; cs->rh = rh; cs->vpd = vpd_kpa(t, rh); }
        if (rs485_owner) { ifan_set(g->fan_base); s_status.ifan_pct = g->fan_base; }
        cs->ifan_pct = g->fan_base;
        return;
    }

    const phase_setpoints_t *s = climate_setpoints(phase);
    // Autoflower-Modus: Photoperiode über den ganzen Grow konstant halten. Eine lokale Kopie
    // der Sollwerte mit gelocktem Lichtplan — Klima/VPD/CO2/PPFD bleiben phasen-abhängig,
    // nur die Lichtzeiten (on/start) werden überschrieben.
    phase_setpoints_t s_aflock;
    if (climate_af_mode(ch) && s) {
        s_aflock = *s;
        s_aflock.light_on_h    = climate_af_light_on(ch);
        s_aflock.light_start_h = climate_af_light_start(ch);
        s = &s_aflock;
    }
    // Zeit-Guard: bei ungültiger Systemzeit (SNTP+RTC aus, Jahr < 2024) NICHT blind nach
    // Garbage-Uhr schalten — letzten bekannten Tag/Nacht-Zustand halten (schützt die
    // Blüte-Dunkelphase vor Lichtleck). Vor je gültiger Zeit: sicher AUS.
    bool day;
    if (now->tm_year >= 124) {                 // 1900 + 124 = 2024
        day = is_light_on(s, now);
        s_last_day[ch] = day; s_had_time[ch] = true;
    } else {
        day = s_had_time[ch] ? s_last_day[ch] : false;
    }
    cs->is_day = day;

    // Soll-Helligkeit JETZT, inkl. DLI-Drossel — zentral berechnet, weil auch die
    // Lampen-Ausfall-Kontrolle damit vergleichen muss (sonst Fehlalarm bei Drossel).
    // DLI-Zielregelung: Tagesziel (mol/m²) erreicht → Helligkeit auf die Grunddrossel
    // senken. Photoperiode bleibt unangetastet (Licht AN, nur dunkler) — wichtig für
    // die Blüte. Ohne PAR-Sensor akkumuliert kein DLI → keine Drossel.
    uint8_t br_now = 0;
    static float s_ppfd_br[N_CHAMBERS] = {0};   // geregelte Helligkeit im PPFD-Modus (je Kammer)
    if (day) {
        br_now = light_brightness(s, now);
        // PPFD-Zielregelung (statt fester %-Helligkeit): langsamer P-Regler mit Deadband passt
        // die Lampen-Helligkeit an, bis der PAR-Sensor den Ziel-PPFD misst. Ohne gültigen
        // PAR-Sensor (oder ppfd_target=0) Fallback auf die feste %-Helligkeit.
        if (climate_light_mode(phase) == 1 && ch == g->par_chamber && d->par_valid && d->ppfd >= 0) {
            uint16_t tgt = climate_ppfd_target(phase);
            if (tgt > 0) {
                const float DEADBAND = 15.0f, KP = 0.05f, MAXSTEP = 2.0f;  // µmol / Verst. / %-Schritt/2s
                if (s_ppfd_br[ch] <= 0.0f) s_ppfd_br[ch] = (float)s->light_pct;  // Init vom Profil-%
                float err = (float)tgt - (float)d->ppfd;       // >0 = zu dunkel
                if (err > DEADBAND || err < -DEADBAND) {
                    float step = err * KP;
                    if (step >  MAXSTEP) step =  MAXSTEP;
                    if (step < -MAXSTEP) step = -MAXSTEP;
                    s_ppfd_br[ch] += step;
                    if (s_ppfd_br[ch] < 1.0f)   s_ppfd_br[ch] = 1.0f;    // Licht bleibt AN
                    if (s_ppfd_br[ch] > 100.0f) s_ppfd_br[ch] = 100.0f;
                }
                br_now = (uint8_t)(s_ppfd_br[ch] + 0.5f);
            }
        }
        float dtgt = climate_dli_target(phase);
        if (ch == g->par_chamber && dtgt > 0 && s_dli_accum >= dtgt && br_now > g->dli_floor_pct)
            br_now = g->dli_floor_pct;   // DLI-Drossel nur in der Kammer mit dem PAR-Sensor
    } else {
        s_ppfd_br[ch] = 0.0f;   // Nacht → Regler-State zurücksetzen (Tag startet wieder vom Profil-%)
    }

    // ── Licht / Photoperiode mit Sonnenauf-/untergang-Rampe ──
    if (day) {
        ch_relay(ch, FN_LIGHT1, true);
        ch_relay(ch, FN_LIGHT2, true);
        ch_dim(ch, br_now);
    } else {
        ch_dim(ch, 0);                       // Dunkelphase-Garantie (Blüte!)
        ch_relay(ch, FN_LIGHT1, false);
        ch_relay(ch, FN_LIGHT2, false);
    }

    // ── Klimasensor der Kammer ──
    float temp, rh;
    bool have = chamber_climate(ch, d, &temp, &rh);
    cs->valid = have;
    if (g->sensor_alarm_en && !have) s_status.alarm_sensor = true;
    if (!have) {
        cs->alarm_temp = cs->alarm_mold = cs->alarm_light = false;
        cs->light_lux = 0;
        // FAILSAFE bei Sensorausfall: geregelte Aktoren AKTIV in sicheren Zustand bringen,
        // sonst heizt/befeuchtet der zuletzt geschaltete Zustand ungeregelt weiter
        // (Überhitzung/Schimmel/CO2). Abluft läuft als Grundlast für Luftaustausch.
        ch_relay(ch, FN_HEATING,    false);   // Heizung aus (sonst unbegrenztes Heizen)
        ch_relay(ch, FN_HUMIDIFIER, false);   // Befeuchter aus (sonst Schimmel)
        ch_relay(ch, FN_DEHUMID,    false);   // Entfeuchter in definierten Zustand
        ch_relay(ch, FN_DEVICE1,    false);   // CO2-Ventil zu (s_co2_on re-synct bei Sensor-Rückkehr)
        if (rs485_owner) { humidifier_set(0); ifan_set(g->fan_base); s_status.ifan_pct = g->fan_base; }
        cs->ifan_pct = g->fan_base;
        return;   // ohne Klimasensor keine Blindregelung
    }

    // Sensor-Kalibrier-Offset dieser Kammer anwenden (driftende RJ12-Sensoren), VOR der VPD-
    // Berechnung — VPD reagiert empfindlich auf rH-Bias. Slot 0/1 = Kammer A/B.
    temp += climate_temp_offset(ch);
    rh   += climate_rh_offset(ch);
    if (rh < 0.0f) rh = 0.0f; if (rh > 100.0f) rh = 100.0f;

    float vpd = vpd_kpa(temp, rh);
    cs->temp = temp; cs->rh = rh; cs->vpd = vpd;
    float temp_target = day ? s->temp_day : s->temp_night;

    // ── Lampen-Ausfall-Kontrolle über den TH3in1-Lichtsensor ──
    // Nur wenn die Kammer den Sensor-Bus-TH3in1 (mit Lichtsensor) als Quelle nutzt.
    // Geprüft wird NUR auf dem Helligkeits-Plateau (nicht während der Sonnenauf-/
    // untergang-Rampe) und erst nach 2 min anhaltender Dunkelheit (Anlauf/Rauschen filtern).
    static int s_light_fault_cnt[N_CHAMBERS] = {0};
    sensor_src_t csrc = SRC_TH_SENSOR;
    climate_get_sensor(ch, &csrc, NULL);
    cs->alarm_light = false;
    cs->light_lux = 0;
    if (g->light_fault_lux > 0 && csrc == SRC_TH_SENSOR && d->th3in1_valid) {
        cs->light_lux = d->light_lux;
        // br_now = Soll-Helligkeit inkl. DLI-Drossel; bei aktiver Drossel ist das
        // Plateau-Kriterium nicht erfüllt → Kontrolle pausiert (kein Fehlalarm).
        bool should_be_lit = day && s->light_pct > 0 && br_now >= s->light_pct;  // volles Plateau
        if (should_be_lit && d->light_lux < (float)g->light_fault_lux) {
            if (s_light_fault_cnt[ch] < 60) s_light_fault_cnt[ch]++;   // 60 × 2 s = 2 min
        } else {
            s_light_fault_cnt[ch] = 0;
        }
        cs->alarm_light = (s_light_fault_cnt[ch] >= 60);
    }

    // Anforderungs-Flags Abluft, je Kammer über Zyklen gehalten (kein Flattern).
    static bool fan_req_temp[N_CHAMBERS] = {0};
    static bool fan_req_hum[N_CHAMBERS]  = {0};

    // ── Temperatur-Alarm (Über- ODER Untertemperatur) ──
    bool over_temp  = (s->temp_alarm > 0.0f)     && (temp > s->temp_alarm);
    bool under_temp = (s->temp_min_alarm > 0.0f) && (temp < s->temp_min_alarm);
    cs->alarm_temp = over_temp || under_temp;
    if (over_temp && g->lockout_dim) ch_dim(ch, s->light_pct / 2);

    // ── VPD-Regelung mit Hysterese ──
    if (vpd < s->vpd_target - s->vpd_deadband) {
        ch_relay(ch, FN_HUMIDIFIER, false);
        if (rs485_owner) humidifier_set(0);
        ch_relay(ch, FN_DEHUMID, true);
        fan_req_hum[ch] = true;
    } else if (vpd > s->vpd_target + s->vpd_deadband) {
        ch_relay(ch, FN_DEHUMID, false);
        ch_relay(ch, FN_HUMIDIFIER, true);
        if (rs485_owner) humidifier_set(2);
        fan_req_hum[ch] = false;
    }

    // ── Temperatur (Heizung) mit Hysterese ──
    if (temp < temp_target - s->temp_deadband) {
        ch_relay(ch, FN_HEATING, true);
        fan_req_temp[ch] = false;
    } else if (temp > temp_target + s->temp_deadband) {
        ch_relay(ch, FN_HEATING, false);
        fan_req_temp[ch] = true;
    }

    // ── CO2 (Sensor global auf Sensor-Bus) — mit Hysterese gegen Ventil-Klappern ──
    // CO2-Sensoren rauschen (±zig ppm). Ohne Totband würde die CO2-Quelle nahe dem Sollwert
    // dauernd takten → Magnetventil-Verschleiß. EIN unter (target − Band), AUS ab target.
    static const int CO2_HYST_PPM = 40;
    static bool s_co2_on[N_CHAMBERS] = {0};
    if (s->co2_target > 0 && d->co2_valid && (day || !s->co2_only_daylight)) {
        if (d->co2_ppm < (int)s->co2_target - CO2_HYST_PPM) s_co2_on[ch] = true;
        else if (d->co2_ppm >= (int)s->co2_target)          s_co2_on[ch] = false;
        ch_relay(ch, FN_DEVICE1, s_co2_on[ch]);
    } else {
        s_co2_on[ch] = false;
        ch_relay(ch, FN_DEVICE1, false);
    }
    if ((s->co2_max_alarm > 0) && d->co2_valid && (d->co2_ppm > s->co2_max_alarm)) {
        s_status.alarm_co2 = true;
        s_co2_on[ch] = false;
        ch_relay(ch, FN_DEVICE1, false);
    }

    // ── Feuchte-/Schimmel-Alarm ──
    cs->alarm_mold = (s->rh_alarm > 0.0f) && (rh > s->rh_alarm);
    if (cs->alarm_mold) ch_relay(ch, FN_DEHUMID, true);

    // ── Abluft-Relais (einfacher Lüfter) mit Anti-Takt-Sperre, je Kammer ──
    bool fan_want = fan_req_temp[ch] || fan_req_hum[ch] || cs->alarm_mold
                    || (over_temp && g->lockout_fan);
    static bool   s_fan_on[N_CHAMBERS]      = {0};
    static time_t s_fan_changed[N_CHAMBERS] = {0};
    time_t tnow = time(NULL);
    if (fan_want) {
        if (!s_fan_on[ch]) { s_fan_on[ch] = true; s_fan_changed[ch] = tnow; }
    } else if (s_fan_on[ch] && (tnow - s_fan_changed[ch] >= (time_t)g->fan_min_cycle_s)) {
        s_fan_on[ch] = false; s_fan_changed[ch] = tnow;
    }
    ch_relay(ch, FN_INLINEFAN, s_fan_on[ch]);

    // ── FREE COOLING: Dachboden/Quellluft nur als Referenz, wenn der eigene
    // Sensor NICHT der Fan-Bus-TH3in1 ist (sonst wäre Referenz == eigene Luft). ──
    float t_over  = temp - temp_target;
    float vpd_low = (s->vpd_target - s->vpd_deadband) - vpd;
    bool  need_cool = t_over  > 0.0f;
    bool  need_dry  = vpd_low > 0.0f;
    bool  ref_ok = d->ref_valid;                              // Dachboden-Referenz (eigene Quelle)
    bool  attic_cooler = ref_ok && (d->ref_temp_c < temp - 1.0f);
    float ah_in  = abs_humidity_gm3(temp, rh);
    float ah_att = ref_ok ? abs_humidity_gm3(d->ref_temp_c, d->ref_humidity_pct) : 999.0f;
    bool  attic_drier = ref_ok && (ah_att < ah_in - 1.0f);
    // Free-Cooling-Rampe NUR, wenn die Quellluft NACHWEISLICH besser ist (gültige Referenz +
    // kühler/trockener). Ohne gültige Referenz (Speicher-Sensor aus) NICHT blind hochfahren —
    // sonst zieht die Abluft womöglich heiße Speicherluft rein (unproduktiv). Stattdessen bleibt
    // sie auf Grundlast und die AC-Feedback-Logik fordert bei Bedarf die Klimaanlage an.
    bool  vent_cool = need_cool && attic_cooler;
    bool  vent_dry  = need_dry  && attic_drier;
    bool  ac_active = false;   // AC kühlt/trocknet diese Kammer → Abluft auf Grundlast drosseln
    if (ch == g->ac_chamber) { // Klimaanlage steht physisch NUR in dieser Kammer (Default A)
        // AC-Anforderung per ECHTEM TEMPERATUR-/VPD-FEEDBACK (statt nur Dachboden-Differenz):
        // Die Abluft versucht zu kühlen/trocknen. Bringt sie über ac_delay_min KEINE messbare
        // Besserung (Temp sinkt nicht ≥COOL_OK, VPD steigt nicht ≥DRY_OK), wird die Klimaanlage
        // angefordert — EGAL WARUM das Lüften versagt (Außenluft zu warm/feucht, Abluft zu
        // schwach/defekt/aus). Tritt messbare Besserung ein, wird das Beobachtungsfenster neu
        // gestartet (kein AC). Latch hält die Anforderung, bis der Bedarf weg ist (keine
        // Oszillation). Timer MONOTON (esp_timer). Referenzwert je Kammer zum Fensterstart.
        static int64_t s_cool_since[N_CHAMBERS]   = {0};
        static int64_t s_dry_since[N_CHAMBERS]    = {0};
        static float   s_cool_t0[N_CHAMBERS]      = {0};   // Temp bei Fensterstart
        static float   s_dry_v0[N_CHAMBERS]       = {0};   // VPD  bei Fensterstart
        static bool    s_ac_cool_latch[N_CHAMBERS]= {0};
        static bool    s_ac_dry_latch[N_CHAMBERS] = {0};
        int64_t now_us   = esp_timer_get_time();
        int64_t delay_us = (int64_t)g->ac_delay_min * 60 * 1000000LL;
        const float COOL_OK = 0.3f;   // °C Mindest-Absenkung pro Fenster = "Lüften kühlt"
        const float DRY_OK  = 0.05f;  // kPa Mindest-VPD-Anstieg     = "Lüften trocknet"

        if (need_cool) {
            // Free-Cooling AUSSICHTSLOS → Klima SOFORT anfordern (ohne ac_delay_min-Beobachtung):
            //  • over_temp: akute Übertemperatur (jede Minute Verzögerung heizt weiter auf), ODER
            //  • ref_ok && !attic_cooler: Quellluft GÜLTIG gemessen und NICHT kühler als die Kammer
            //    → Lüften kann per Definition nicht kühlen, also direkt die Klimaanlage.
            // Bei FEHLENDER Referenz (Speicher-Sensor aus) weiter die normale Feedback-Beobachtung.
            // Latch bleibt gesetzt, bis need_cool (temp ≤ temp_target) wegfällt.
            if (over_temp || (ref_ok && !attic_cooler)) {
                s_ac_cool_latch[ch] = true;
            } else if (!s_cool_since[ch]) { s_cool_since[ch] = now_us; s_cool_t0[ch] = temp; }
            else if (!s_ac_cool_latch[ch] && (now_us - s_cool_since[ch] >= delay_us)) {
                if (temp <= s_cool_t0[ch] - COOL_OK) {        // kühlt erfolgreich → Fenster neu
                    s_cool_since[ch] = now_us; s_cool_t0[ch] = temp;
                } else {
                    s_ac_cool_latch[ch] = true;               // keine Besserung → AC
                }
            }
        } else { s_cool_since[ch] = 0; s_ac_cool_latch[ch] = false; }

        if (need_dry) {
            if (!s_dry_since[ch]) { s_dry_since[ch] = now_us; s_dry_v0[ch] = vpd; }
            else if (!s_ac_dry_latch[ch] && (now_us - s_dry_since[ch] >= delay_us)) {
                if (vpd >= s_dry_v0[ch] + DRY_OK) {           // trocknet erfolgreich → Fenster neu
                    s_dry_since[ch] = now_us; s_dry_v0[ch] = vpd;
                } else {
                    s_ac_dry_latch[ch] = true;
                }
            }
        } else { s_dry_since[ch] = 0; s_ac_dry_latch[ch] = false; }

        bool ac_cool = s_ac_cool_latch[ch];
        bool ac_dry  = s_ac_dry_latch[ch];
        ac_active = ac_cool || ac_dry;
        if (ac_active) s_status.ac_demand = true;
        s_status.ac_mode |= (ac_cool ? 1 : 0) | (ac_dry ? 2 : 0);
        s_ac_throttle = ac_active;   // für den iFan-Eigner sichtbar machen (auch andere Kammer)
    }

    // ── Stufenloser RS-485-iFan: Grundlast + Rampe (nur Eigner steuert physisch) ──
    int spd;
    if (ac_active || (s_ac_throttle && rs485_owner)) {
        // AC kühlt/trocknet aktiv → Abluft NUR Grundlast, sonst bläst sie die gekühlte Luft raus
        // und zieht warme/feuchte Luft nach. VORRANG, auch bei Übertemperatur — sonst arbeiten
        // Klima und Abluft gegeneinander. s_ac_throttle greift auch, wenn AC-Kammer != iFan-Eigner.
        spd = g->fan_base;
    } else if (over_temp || cs->alarm_mold) {
        // Übertemperatur/Schimmel OHNE aktive AC: Vollabluft NUR, wenn die Quellluft das wirklich
        // verbessert (nachweislich kühler bzw. trockener) ODER gar keine Referenz existiert (dann
        // ist Notentlüftung die einzige Option). Ist die Außenluft messbar schlechter (heißer
        // Sommer-Speicher), bringt Vollgas nichts und zieht nur wärmere Luft rein → Grundlast.
        bool vent_helps = over_temp ? (!ref_ok || attic_cooler)
                                    : (!ref_ok || attic_drier);   // sonst: alarm_mold
        spd = vent_helps ? g->fan_max : g->fan_base;
    } else {
        int add = 0;
        if (vent_cool) add = (int)(t_over * 18.0f);
        if (vent_dry)  { int a = (int)(vpd_low * 120.0f); if (a > add) add = a; }
        spd = g->fan_base + add;
    }
    if (spd < g->fan_base) spd = g->fan_base;
    if (spd > g->fan_max)  spd = g->fan_max;
    cs->ifan_pct = (uint8_t)spd;
    if (rs485_owner) {
        ifan_set(spd); s_status.ifan_pct = (uint8_t)spd;
        // Nur die TATSÄCHLICHE Notfall-Vollabluft (over_temp/Schimmel UND Quellluft hilft → fan_max)
        // übersteuert den manuellen Override (Sicherheit vor Komfort). Bleibt die Abluft auf
        // Grundlast (AC kühlt / Außenluft schlechter), behält ein manueller iFan-Override Vorrang.
        if ((over_temp || cs->alarm_mold) && spd >= (int)g->fan_max) s_ifan_force_max = true;
    }
}

static void regulate(const sensor_data_t *d, const struct tm *now)
{
    // Aggregat-Alarme/AC pro Zyklus zurücksetzen, dann je Kammer ODER-verknüpfen.
    s_status.alarm_co2 = false;
    s_status.alarm_sensor = false;
    s_status.ac_demand = false;
    s_status.ac_mode   = 0;
    s_ifan_force_max = false;   // pro Zyklus neu; die Eigner-Kammer setzt es bei over_temp/Schimmel
    // Keine AC-Kammer konfiguriert → Drossel-Flag sicher löschen (die AC-Kammer setzt es sonst
    // jeden Zyklus frisch; nur der -1/„keine AC"-Fall bleibt sonst hängen).
    { const grow_global_t *gg = climate_global();
      if (gg->ac_chamber < 0 || gg->ac_chamber >= N_CHAMBERS) s_ac_throttle = false; }

    for (int ch = 0; ch < N_CHAMBERS; ch++) regulate_chamber(ch, d, now);

    // ── Aggregat für LEDs/MQTT/Status (Kammer A führend bei Einzelwerten) ──
    s_status.phase   = climate_get_phase(0);
    s_status.is_day  = s_cstate[0].is_day;
    s_status.vpd_now = s_cstate[0].vpd;
    s_status.alarm_temp = s_cstate[0].alarm_temp || s_cstate[1].alarm_temp;
    s_status.alarm_mold = s_cstate[0].alarm_mold || s_cstate[1].alarm_mold;
    s_status.alarm_light = s_cstate[0].alarm_light || s_cstate[1].alarm_light;
    s_status.alarm_time  = (now->tm_year < 124);   // Systemzeit ungültig → Photoperiode angehalten

    // ── Wasserwert-Alarm (pH/Wassertemp/ORP außerhalb Korridor; Werte von HA via MQTT) ──
    // Nur bei aktiviertem Schutz UND frischen Werten (HA aus/Tester offline → kein Alarm).
    s_status.alarm_water = false;
    const grow_global_t *gw = climate_global();
    if (gw->water_alarm_en) {
        water_data_t w; state_get_water(&w);
        if (water_is_fresh(&w, 300)) {
            bool a = false;
            if (w.ph_valid) {
                if (gw->water_ph_min > 0 && w.ph < gw->water_ph_min) a = true;
                if (gw->water_ph_max > 0 && w.ph > gw->water_ph_max) a = true;
            }
            if (w.temp_valid) {
                if (gw->water_temp_max > 0 && w.temp_c > gw->water_temp_max) a = true;
                if (gw->water_temp_min > 0 && w.temp_c < gw->water_temp_min) a = true;
            }
            if (w.orp_valid && gw->water_orp_min != 0 && w.orp_mv < (float)gw->water_orp_min) a = true;
            s_status.alarm_water = a;
        }
    }

    // ── DLI-Integration (PPFD über Zeit) — der EINE PAR-Sensor sitzt in par_chamber ──
    // Tageswechsel nur bei GÜLTIGER Zeit erkennen — sonst würde die Boot-Uhr (1970)
    // eine aus dem Flash wiederhergestellte Tagessumme sofort nullen.
    if (now->tm_year >= 124 && s_last_yday != now->tm_yday) {
        s_dli_accum = 0; s_last_yday = now->tm_yday;
    }
    // Nur akkumulieren, wenn die PAR-Kammer Tag hat (früher: „irgendeine Kammer" → zählte
    // Fremdlicht der anderen Kammer mit). Plausi-Guard gegen defekten Sensor (0xFFFF).
    int parc = climate_global()->par_chamber; if (parc < 0 || parc >= N_CHAMBERS) parc = 0;
    if (d->par_valid && d->ppfd > 0 && d->ppfd < 5000 && s_cstate[parc].is_day)
        s_dli_accum += (d->ppfd * 2.0f) / 1e6f;   // µmol/m²/s × 2s → mol/m²
    s_status.dli_today = s_dli_accum;
}

// Bewässerungs-Sicherheitsabschaltung: läuft die Bewässerung länger als water_max_min am
// Stück, wird sie zwangsweise AUS geschaltet (Überflutungsschutz) + Buzzer/Log.
// WICHTIG: Basiert auf esp_timer_get_time() (MONOTON), NICHT auf time(NULL). Ein SNTP-
// Rückwärtssprung der Wanduhr während des Laufs darf die Abschaltung niemals aushebeln.
static void check_watering_timeout(void)
{
    static int64_t s_water_since_us = 0;
    const grow_global_t *g = climate_global();
    if (g->water_max_min == 0) { s_water_since_us = 0; return; }   // Schutz deaktiviert
    int p = relay_role_find(FN_WATERING);   // gleiche Auflösung wie die Bewässerungs-Automatik
    bool on = (p >= 0) && relay_get((relay_id_t)p);
    if (!on) { s_water_since_us = 0; return; }
    int64_t now_us = esp_timer_get_time();
    if (s_water_since_us == 0) { s_water_since_us = now_us; return; }   // gerade angegangen
    if ((now_us - s_water_since_us) >= (int64_t)g->water_max_min * 60 * 1000000LL) {
        relay_set_manual((relay_id_t)p, false);                    // zwangsweise AUS
        s_water_since_us = 0;
        buzzer_tone_ms(2700, 3000);
        ESP_LOGW(TAG, "Bewässerung > %u min → Sicherheitsabschaltung", g->water_max_min);
    }
}

// Dieser Task ist der EINZIGE Modbus-Bus-Nutzer (poll) + State-Schreiber.
static void task(void *arg)
{
    sensor_data_t d;
    power_data_t pw;
    time_t alarm_last = 0;
    // Selbstüberwachung: der Klima-Task ist der EINZIGE Aktor-Steuerer. Hängt er, bleiben Relais
    // im letzten Zustand — daher beim Task-Watchdog registrieren (Reboot → sichere AUS-Defaults).
    esp_task_wdt_add(NULL);   // ignoriert, falls TWDT nicht initialisiert
    while (true) {
        esp_task_wdt_reset();
        time_t now_t = time(NULL);
        struct tm now; localtime_r(&now_t, &now);

        devices_poll(&d);            // Sensor-Bus + Fan-Bus
        apply_attic_source(&d);      // Dachboden-Quelle (Fan-Bus / BLE / Sensor-Bus)
        state_set_sensors(&d);

        if (bl0940_read(&pw) == ESP_OK) state_set_power(&pw);

        regulate(&d, &now);          // Aktoren stellen
        int ifan_man = climate_get_ifan_manual();
        if (ifan_man >= 0 && !s_ifan_force_max) {   // manueller Abluft-Override — AUSSER die
            ifan_set((uint8_t)ifan_man);            // Notfall-Vollabluft (over_temp/Schimmel) hat
            s_status.ifan_pct = (uint8_t)ifan_man;  // Vorrang (Sicherheit vor Komfort)
        }
        int humid_man = climate_get_humid_manual();
        if (humid_man >= 0) humidifier_set(humid_man);   // Befeuchter-Override
        if (climate_clipfan_consume_dirty()) {           // Clip-Fan-Direktbefehl (nicht geregelt)
            int st, sw; bool nat;
            climate_get_clipfan(&st, &sw, &nat);
            clipfan_set(st, sw, nat);
        }
        check_light_warning(&now);   // 15s-Ton 5 min vor Licht-Aus
        check_watering_timeout();        // Überflutungsschutz Bewässerung (monotone Zeit)
        watering_tick(&d, now_t);        // Bewässerungs-Automatik (Intervall/Substratfeuchte)
        relay_sched_tick(&now);          // Steckdosen-Zeitpläne (Tagesplan/Zyklus)
        state_set_climate(&s_status);
        history_tick(now_t, &d, &pw, &s_status);   // Langzeit-Verlauf (alle 5 min ein Sample)
        history_persist_tick(now_t, climate_global()->hist_save_min,
                             s_dli_accum, s_last_yday);   // Flash-Sicherung (konfigurierbar)

        // ── Akustischer Alarm, Quittierung PRO Alarmtyp ──
        // Quittierter Alarm bleibt stumm, bis er normalisiert war; danach (oder bei
        // einem anderen Alarmtyp) piept es wieder. Wiederhol-Intervall konfigurierbar.
        bool a_temp = s_status.alarm_temp, a_mold = s_status.alarm_mold,
             a_co2  = s_status.alarm_co2,  a_sens = s_status.alarm_sensor,
             a_light = s_status.alarm_light, a_water = s_status.alarm_water;
        // Alarm-Flanken protokollieren (an/aus mit Zeitstempel) — nur bei gültiger Zeit.
        uint8_t curmask = (a_temp?1:0)|(a_mold?2:0)|(a_co2?4:0)|(a_sens?8:0)|(a_light?16:0)|(a_water?32:0);
        if (now.tm_year >= 124 && curmask != s_alarm_prev) {
            for (int b = 0; b < 6; b++)
                if ((curmask ^ s_alarm_prev) & (1 << b)) alog_push(b, (curmask >> b) & 1, (uint32_t)now_t);
            s_alarm_prev = curmask;
        }
        bool ack_changed = false;
        if (s_ack_req) {                       // BT-Quittierung: aktive Alarme stummschalten
            s_ack_req = false;
            s_ack_temp = a_temp; s_ack_mold = a_mold; s_ack_co2 = a_co2; s_ack_sens = a_sens;
            s_ack_light = a_light; s_ack_water = a_water;
            ack_changed = true;
        }
        if (!a_temp && s_ack_temp) { s_ack_temp = false; ack_changed = true; }  // normalisiert → wieder scharf
        if (!a_mold && s_ack_mold) { s_ack_mold = false; ack_changed = true; }
        if (!a_co2  && s_ack_co2)  { s_ack_co2  = false; ack_changed = true; }
        if (!a_sens && s_ack_sens) { s_ack_sens = false; ack_changed = true; }
        if (!a_light && s_ack_light) { s_ack_light = false; ack_changed = true; }
        if (!a_water && s_ack_water) { s_ack_water = false; ack_changed = true; }
        if (ack_changed) ack_save();           // Quittier-Status persistieren (selten → kein Flash-Verschleiß)
        bool buzz = (a_temp && !s_ack_temp) || (a_mold && !s_ack_mold)
                  || (a_co2 && !s_ack_co2)  || (a_sens && !s_ack_sens)
                  || (a_light && !s_ack_light) || (a_water && !s_ack_water);
        const grow_global_t *g = climate_global();
        if (buzz && g->buzzer_enable && (now_t - alarm_last > g->alarm_repeat_s)) {
            buzzer_alarm();
            alarm_last = now_t;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void climate_start(void)
{
    xTaskCreate(task, "climate", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Regelung läuft (A:%s B:%s)",
             climate_phase_name(climate_get_phase(0)), climate_phase_name(climate_get_phase(1)));
}
