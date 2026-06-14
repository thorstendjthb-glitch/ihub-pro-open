// climate_config.c — Klima-Teilmodul: Konfiguration + Persistenz.
// Besitzt Sollwerte, Phasen-/Kammer-/Sensor-Zuordnung, globale Einstellungen,
// Grow-Zyklus und die manuellen Overrides (iFan/Befeuchter/Clip-Fan) inkl. NVS.
// Die Regelung selbst liegt in climate_control.c und liest alles über die
// öffentliche API (climate_setpoints, climate_global, climate_get_* …).
#include "climate_control.h"
#include "climate_internal.h"
#include "nvsutil.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "climate";

// Default-Sollwerte je Phase (aus docs/GROW_FEATURES.md). Editierbar.
// Felder: light_on_h, light_start_h, vpd, vpd_db, t_day, t_night, t_db,
//         rh_day, rh_night, co2, co2_only_daylight, light_pct, ramp_min,
//         temp_alarm(°C,0=aus), rh_alarm(%,0=aus), temp_min_alarm(°C,0=aus), co2_max_alarm(ppm,0=aus)
// Recherchierte, INTERN KONSISTENTE Cannabis-Sollwerte (2026-06-14): rh_day/rh_night sind so
// gewählt, dass VPD(temp_day, rh_day) ≈ vpd_target — sonst meldet das Dashboard „zu trocken",
// obwohl Temp+rH grün sind (VPD kombiniert beides nichtlinear). Quellen: VPD-/PPFD-/DLI-Charts.
//   Sämlinge VPD 0.65 (0.4–0.8), rH ~78 % · Stecklinge 0.55, rH ~82 % (Wurzeln fehlen → feucht)
//   Wuchs 1.0 (0.8–1.2), rH ~70 % · Blüte 1.4 (1.2–1.6), rH ~56 %/40 % (trockene Nächte = Schimmelschutz)
//   Automatics 1.1 (0.8–1.4), rH ~65 % · Trocknen ~0.85 → rH ~60 % bei 19 °C (60/60-Regel)
// Felder: light_on_h, light_start_h, vpd, vpd_db, t_day, t_night, t_db, rh_day, rh_night, co2,
//         co2_only_daylight, light_pct, ramp_min, temp_alarm, rh_alarm, temp_min_alarm, co2_max_alarm
static phase_setpoints_t sp[PHASE_MAX] = {
    [PHASE_SEEDS]   = {18, 6, 0.65f, 0.20f, 24, 22, 1.0f, 78, 75,    0, 1, 30, 15, 30, 90, 16, 0},
    [PHASE_CLONES]  = {18, 6, 0.55f, 0.20f, 24, 22, 1.0f, 82, 79,    0, 1, 25, 15, 30, 92, 16, 0},
    [PHASE_VEG]     = {18, 6, 1.00f, 0.20f, 26, 22, 1.0f, 70, 62, 1000, 1, 80, 15, 31, 80, 15, 1800},
    [PHASE_FLOWER]  = {12, 8, 1.40f, 0.20f, 25, 20, 1.0f, 56, 40, 1200, 1, 100, 20, 30, 68, 15, 2000},
    [PHASE_AUTO]    = {20, 5, 1.10f, 0.30f, 25, 22, 1.0f, 65, 58,  900, 1, 90, 15, 31, 75, 15, 1800},
    [PHASE_DRY]     = { 0, 0, 0.85f, 0.20f, 19, 19, 2.0f, 61, 61,    0, 0, 0, 0, 25, 70, 10, 0},
};

// Globale Defaults (profil-übergreifend).
static grow_global_t g_cfg = {
    .buzzer_enable = 1, .alarm_repeat_s = 60, .fan_min_cycle_s = 60,
    .lockout_dim = 1, .lockout_fan = 1, .sensor_alarm_en = 1,
    .fan_base = 10, .fan_max = 100, .light_fault_lux = 100,
    .water_max_min = 30,    // Bewässerung: Sicherheitsabschaltung nach 30 min Dauerlauf
    .hist_save_min = 60,    // Verlauf+DLI stündlich in den Flash sichern (0 = aus)
    .dli_floor_pct = 30,    // DLI-Ziel erreicht → Licht auf 30 % drosseln
    // Wasserwert-Alarme: Default AUS (User aktiviert, wenn echte Nährlösung läuft —
    // sonst Fehlalarm bei Testwasser). Schwellen = RDWC-Korridore (docs/RDWC_KNOWLEDGE.md).
    .water_alarm_en = 0,
    .water_ph_min = 5.8f, .water_ph_max = 6.2f,
    .water_temp_max = 22.0f, .water_temp_min = 0.0f,   // temp_min 0 = aus
    .water_orp_min = 0,                                // 0 = aus (ORP-Korridor setup-abhängig)
    .ac_delay_min = 15,    // 15 min Free-Cooling-Vorrang, dann erst Klimaanlage anfordern
    .co2_baseline = 420,   // CO2-Frischluft-Baseline (FRC): Reg10 + 420 ppm
    .ac_chamber = 0,       // Klimaanlage in Kammer A
    .par_chamber = 0,      // PAR/PPFD-Sensor in Kammer A
};

// DLI-Tagesziele je Phase (mol/m², 0 = aus) — separater Blob, siehe climate_control.h.
// DLI-Tagesziele je Phase (mol/m²/Tag), recherchiert: Sämling 12, Steckling 10, Wuchs 30,
// Blüte 40, Auto 30, Trocknen 0. Greift nur mit PAR-Sensor; bei Erreichen → Licht auf dli_floor.
static float s_dli_tgt[PHASE_MAX] = { 12, 10, 30, 40, 30, 0 };

float climate_dli_target(grow_phase_t p) { return p < PHASE_MAX ? s_dli_tgt[p] : 0; }

void climate_set_dli_target(grow_phase_t p, float mol)
{
    if (p >= PHASE_MAX) return;
    if (mol < 0) mol = 0;
    if (mol > 100) mol = 100;
    s_dli_tgt[p] = mol;
}

void climate_save_dli_targets(void)
{
    if (nvsu_save("grow", "dlitgt", s_dli_tgt, sizeof(s_dli_tgt)))
        ESP_LOGI(TAG, "DLI-Ziele gespeichert");
}

// PPFD-Lichtregelung je Phase — separate Blobs (wie DLI), siehe climate_control.h.
// light_mode: 0 = feste Helligkeit (light_pct), 1 = PPFD-Zielregelung (regelt Helligkeit,
// bis der PPFD-Sensor den Zielwert misst). ppfd_tgt = Ziel in µmol/m²/s.
// Defaults: sinnvolle Cannabis-Werte je Phase (Keimling niedrig → Blüte hoch).
static uint8_t  s_light_mode[PHASE_MAX] = { 0, 0, 0, 0, 0, 0 };
static uint16_t s_ppfd_tgt[PHASE_MAX]   = { 200, 150, 500, 800, 500, 0 };
//                                          Seed Clone Veg Flower Auto Dry
// (recherchiert: Sämling 100–300, Steckling 100–200, Wuchs 400–600, Blüte 700–900, Auto <700)

uint8_t  climate_light_mode(grow_phase_t p)  { return p < PHASE_MAX ? s_light_mode[p] : 0; }
uint16_t climate_ppfd_target(grow_phase_t p) { return p < PHASE_MAX ? s_ppfd_tgt[p]   : 0; }

void climate_set_light_mode(grow_phase_t p, uint8_t mode)
{
    if (p < PHASE_MAX) s_light_mode[p] = mode ? 1 : 0;
}

void climate_set_ppfd_target(grow_phase_t p, uint16_t ppfd)
{
    if (p >= PHASE_MAX) return;
    if (ppfd > 3000) ppfd = 3000;   // Plausi-Obergrenze (sehr hell)
    s_ppfd_tgt[p] = ppfd;
}

void climate_save_light_ppfd(void)
{
    nvsu_save("grow", "lmode", s_light_mode, sizeof(s_light_mode));
    if (nvsu_save("grow", "ppfdtgt", s_ppfd_tgt, sizeof(s_ppfd_tgt)))
        ESP_LOGI(TAG, "PPFD-Lichtregelung gespeichert");
}

// ── Sensor-Kalibrier-Offsets je Slot (0/1 = Kammer A/B, 2 = Dachboden) ──
// Additive Korrektur der (driftenden) RJ12-Sensoren VOR der VPD-Berechnung. Default 0.
static float s_temp_off[N_SENS] = { 0 };   // °C
static float s_rh_off[N_SENS]   = { 0 };   // %rH

float climate_temp_offset(int slot) { return (slot >= 0 && slot < N_SENS) ? s_temp_off[slot] : 0.0f; }
float climate_rh_offset(int slot)   { return (slot >= 0 && slot < N_SENS) ? s_rh_off[slot]   : 0.0f; }

void climate_set_offsets(int slot, float temp_off, float rh_off)
{
    if (slot < 0 || slot >= N_SENS) return;
    if (temp_off < -10.0f) temp_off = -10.0f; if (temp_off > 10.0f) temp_off = 10.0f;   // Plausi
    if (rh_off   < -20.0f) rh_off   = -20.0f; if (rh_off   > 20.0f) rh_off   = 20.0f;
    s_temp_off[slot] = temp_off; s_rh_off[slot] = rh_off;
}

void climate_save_offsets(void)
{
    nvsu_save("grow", "tempoff", s_temp_off, sizeof(s_temp_off));
    if (nvsu_save("grow", "rhoff", s_rh_off, sizeof(s_rh_off)))
        ESP_LOGI(TAG, "Sensor-Offsets gespeichert");
}

static const char *k_names[PHASE_MAX] = {
    "Seeds", "Stecklinge", "Wuchs", "Bluete", "Automatics", "Trocknen"
};

// ── 2 Kammern: je eigene Phase + eigene Klimasensor-Quelle ──
// Default: Kammer A = Sensor-Bus-TH3in1 (wie bisher), Kammer B = AUS + Fan-Bus-TH3in1.
// Kammer B greift bei AUS auf nichts zu; zusätzlich besitzt B per Default keine
// Aktoren (alle Steckdosen/Dimmer/iFan = Kammer A) → Verhalten exakt wie vorher.
typedef struct { uint8_t src; uint8_t mac[6]; } csens_t;   // src = sensor_src_t

static grow_phase_t s_phase[N_CHAMBERS]   = { PHASE_VEG, PHASE_OFF };
// Klima-Automatik je Kammer ein/aus. AUS = nur Messwerte zeigen, KEINE Aktoren regeln
// (Lichter/Relais/iFan/Befeuchter bleiben auf manuell/letztem Wert). Default: an.
static bool s_chamber_auto[N_CHAMBERS]     = { true, true };
// Sensor-Quellen: Index 0/1 = Kammer A/B, Index N_CHAMBERS = Dachboden/Quellluft (Free-Cooling).
static csens_t      s_csens[N_SENS]       = {
    { SRC_TH_SENSOR, {0} },   // Kammer A
    { SRC_TH_FAN,    {0} },   // Kammer B
    { SRC_TH_FAN,    {0} },   // Dachboden (Default Fan-Bus wie bisher)
};

// Persistenz der manuellen Overrides (iFan/Humid/Clip) — NUR bei manueller Änderung
// geschrieben (nicht in der Regelschleife → kein Flash-Wear), beim Boot wiederhergestellt.
static void manual_save(void);

// Manueller Abluft-iFan-Override (-1 = Automatik). Wird am Ende jedes Regel-Zyklus angewandt.
static volatile int s_ifan_manual = -1;
void climate_set_ifan_manual(int pct) {
    int v = (pct < 0) ? -1 : (pct > 100 ? 100 : pct);
    if (v == s_ifan_manual) return;            // nur bei echter Änderung persistieren
    s_ifan_manual = v; manual_save();
}
int  climate_get_ifan_manual(void)    { return s_ifan_manual; }

// Manueller Luftbefeuchter-Override (-1 = Automatik/VPD).
static volatile int s_humid_manual = -1;
void climate_set_humid_manual(int level) {
    int v = (level < 0) ? -1 : (level > 4 ? 4 : level);
    if (v == s_humid_manual) return;           // nur bei echter Änderung persistieren
    s_humid_manual = v; manual_save();
}
int  climate_get_humid_manual(void)      { return s_humid_manual; }

// Clip-Fan: reine Direktsteuerung (nicht geregelt). Wird bei Änderung im Regel-Task
// gesetzt (kein RS-485-Konflikt mit dem Poll). s_clip_dirty triggert den Schreibvorgang.
static volatile int  s_clip_stufe = 0, s_clip_schwenk = 0;
static volatile bool s_clip_natural = false, s_clip_dirty = false;
void climate_set_clipfan(int stufe, int schwenk, bool natural)
{
    int st = stufe   < 0 ? 0 : (stufe   > 10 ? 10 : stufe);
    int sw = schwenk < 0 ? 0 : (schwenk > 10 ? 10 : schwenk);   // 0/5/10; auf 0..10 begrenzt (uint8_t-sicher)
    bool changed = (st != s_clip_stufe) || (sw != s_clip_schwenk) || (natural != s_clip_natural);
    s_clip_stufe   = st;
    s_clip_schwenk = sw;
    s_clip_natural = natural;
    s_clip_dirty   = true;          // Direktbefehl: immer neu anwenden (RS-485, kein Flash)
    if (changed) manual_save();     // aber nur bei echter Änderung persistieren
}
void climate_get_clipfan(int *stufe, int *schwenk, bool *natural)
{
    if (stufe)   *stufe   = s_clip_stufe;
    if (schwenk) *schwenk = s_clip_schwenk;
    if (natural) *natural = s_clip_natural;
}

bool climate_clipfan_consume_dirty(void)
{
    if (!s_clip_dirty) return false;
    s_clip_dirty = false;
    return true;
}

// Manuelle Overrides als EIN gepacktes Blob in NVS (Namespace "grow", Key "manual").
// i8 für iFan/Humid (-1 = Auto darstellbar). Ein Blob = ein Commit.
typedef struct {
    int8_t  ifan;          // -1 = Auto, sonst 0..100
    int8_t  humid;         // -1 = Auto, sonst 0..4
    uint8_t clip_stufe;    // 0..10
    uint8_t clip_schwenk;  // 0/5/10
    uint8_t clip_natural;  // 0/1
} manual_blob_t;

static void manual_save(void)
{
    manual_blob_t m = {
        .ifan         = (int8_t)s_ifan_manual,
        .humid        = (int8_t)s_humid_manual,
        .clip_stufe   = (uint8_t)s_clip_stufe,
        .clip_schwenk = (uint8_t)s_clip_schwenk,
        .clip_natural = s_clip_natural ? 1 : 0,
    };
    nvsu_save("grow", "manual", &m, sizeof(m));
}
static void manual_load(void)
{
    manual_blob_t m;
    if (!nvsu_load("grow", "manual", &m, sizeof(m))) return;
    s_ifan_manual  = m.ifan;
    s_humid_manual = m.humid;
    s_clip_stufe   = m.clip_stufe;
    s_clip_schwenk = m.clip_schwenk;
    s_clip_natural = (m.clip_natural != 0);
    // Clip-Fan beim Boot einmal anwenden (iFan/Befeuchter macht der Regel-Task ohnehin).
    if (s_clip_stufe || s_clip_schwenk || s_clip_natural) s_clip_dirty = true;
}

static void load_phase_nvs(void)
{
    uint8_t p;
    // Rückwärtskompatibel: alter Einzelschlüssel "phase" → Kammer A
    if (nvsu_get_u8("grow", "phase", &p) && p <= PHASE_OFF) s_phase[0] = p;
    if (nvsu_get_u8("grow", "ph0",   &p) && p <= PHASE_OFF) s_phase[0] = p;
    if (nvsu_get_u8("grow", "ph1",   &p) && p <= PHASE_OFF) s_phase[1] = p;
    uint8_t a;
    if (nvsu_get_u8("grow", "auto0", &a)) s_chamber_auto[0] = (a != 0);
    if (nvsu_get_u8("grow", "auto1", &a)) s_chamber_auto[1] = (a != 0);
    // Sensor-Zuordnung (still, falls fehlt); Teil-Laden für ältere, kürzere Blobs.
    nvsu_load_partial("grow", "csens", s_csens, sizeof(s_csens), NULL);
}

void climate_save_setpoints(void)
{
    if (nvsu_save("grow", "setpoints", sp, sizeof(sp)))
        ESP_LOGI(TAG, "Sollwerte gespeichert");
}

void climate_load_setpoints(void)
{
    // nvsu_load lädt NUR bei exakter Größen-Übereinstimmung — sonst würde ein alter,
    // kleinerer Blob die größere Struktur korrumpieren. Bei Mismatch greifen Defaults.
    if (nvsu_load("grow", "setpoints", sp, sizeof(sp))) {
        ESP_LOGI(TAG, "Sollwerte aus NVS geladen");
    } else {
        size_t sz = nvsu_blob_size("grow", "setpoints");
        if (sz) ESP_LOGW(TAG, "NVS-Sollwerte verworfen (Größe %u != %u) → Defaults",
                         (unsigned)sz, (unsigned)sizeof(sp));
    }
}

grow_global_t *climate_global(void) { return &g_cfg; }

void climate_save_global(void)
{
    if (nvsu_save("grow", "global", &g_cfg, sizeof(g_cfg)))
        ESP_LOGI(TAG, "Globale Config gespeichert");
}

static void load_global_nvs(void)
{
    // Vorwärtskompatibel: einen ÄLTEREN (kürzeren) Blob teilweise laden — neue Felder
    // am Ende behalten ihren Default. So gehen bei FW-Updates, die grow_global_t
    // erweitern, NICHT alle globalen Einstellungen verloren.
    size_t sz = 0;
    if (nvsu_load_partial("grow", "global", &g_cfg, sizeof(g_cfg), &sz)) {
        ESP_LOGI(TAG, "Globale Config aus NVS geladen (%u/%u Bytes)",
                 (unsigned)sz, (unsigned)sizeof(g_cfg));
        // Tail-Padding-Falle: ac_delay_min liegt im früheren Padding (Offset 38). Ein alter,
        // kürzerer Blob ist größengleich genug, dass das 0-Padding ac_delay_min überschreibt
        // → würde "AC sofort" bedeuten. Älteren Blob an der Größe erkennen und Default setzen.
        if (sz < sizeof(g_cfg) && g_cfg.ac_delay_min == 0) g_cfg.ac_delay_min = 15;
    }
}

// ── Grow-Zyklus (Ernte-Timer/Kalender) — PRO KAMMER ──
static uint32_t s_grow_start[N_CHAMBERS] = {0, 0};   // Unix-Epoch (0 = nicht gesetzt)
static uint16_t s_grow_days[N_CHAMBERS]  = {90, 90}; // erwartete Gesamtdauer in Tagen

// ── Grow-Plan je Kammer (separate NVS-Blobs) ──
// Default = klassischer Photoperioden-Grow: 1 Woche Keimung, 2 Wochen Wuchs, 9 Wochen Blüte.
static growplan_step_t s_growplan[N_CHAMBERS][GROWPLAN_STEPS] = {
    { {PHASE_SEEDS,7}, {PHASE_VEG,14}, {PHASE_FLOWER,63}, {0,0}, {0,0}, {0,0} },
    { {PHASE_SEEDS,7}, {PHASE_VEG,14}, {PHASE_FLOWER,63}, {0,0}, {0,0}, {0,0} },
};
static uint8_t s_plan_en[N_CHAMBERS] = {0, 0};   // Default: Plan aus → manuelle Phasenwahl

// ── Autoflower-Modus (Photoperiode-Lock) je Kammer ──
static uint8_t s_af_mode[N_CHAMBERS]   = {0, 0};    // 0 = aus (Licht je Phase), 1 = fester Lichtplan
static uint8_t s_af_lon[N_CHAMBERS]    = {20, 20};  // Licht-Stunden/Tag (Default 20/4)
static uint8_t s_af_lstart[N_CHAMBERS] = {6, 6};    // Start-Stunde

static void load_grow_nvs(void)
{
    // rückwärtskompatibel: alter Einzel-Key gstart/gdays → Kammer A
    nvsu_get_u32("grow", "gstart", &s_grow_start[0]);
    nvsu_get_u16("grow", "gdays",  &s_grow_days[0]);
    nvsu_get_u32("grow", "gstart0", &s_grow_start[0]); nvsu_get_u16("grow", "gdays0", &s_grow_days[0]);
    nvsu_get_u32("grow", "gstart1", &s_grow_start[1]); nvsu_get_u16("grow", "gdays1", &s_grow_days[1]);
    nvsu_load_partial("grow", "plan_en",  s_plan_en,  sizeof(s_plan_en),  NULL);
    nvsu_load_partial("grow", "growplan", s_growplan, sizeof(s_growplan), NULL);
    nvsu_load_partial("grow", "af_mode",  s_af_mode,  sizeof(s_af_mode),  NULL);
    nvsu_load_partial("grow", "af_lon",   s_af_lon,   sizeof(s_af_lon),   NULL);
    nvsu_load_partial("grow", "af_lst",   s_af_lstart, sizeof(s_af_lstart), NULL);
}

static grow_phase_t planned_phase(int ch);   // Forward-Decl (climate_plan_info nutzt sie)

bool    climate_af_mode(int ch)        { return (ch >= 0 && ch < N_CHAMBERS) ? s_af_mode[ch] != 0 : false; }
uint8_t climate_af_light_on(int ch)    { return (ch >= 0 && ch < N_CHAMBERS) ? s_af_lon[ch]   : 20; }
uint8_t climate_af_light_start(int ch) { return (ch >= 0 && ch < N_CHAMBERS) ? s_af_lstart[ch]: 6; }

void climate_set_af(int ch, uint8_t mode, uint8_t on, uint8_t start)
{
    if (ch < 0 || ch >= N_CHAMBERS) return;
    s_af_mode[ch] = mode ? 1 : 0;
    if (on <= 24)    s_af_lon[ch]    = on;
    if (start <= 23) s_af_lstart[ch] = start;
}

// Plan-Übersicht (Dashboard): aktueller Tag, Gesamtdauer, aktuelle/nächste Phase, Tage bis Wechsel.
void climate_plan_info(int ch, int *day, int *total, int *cur, int *next, int *next_in)
{
    if (day) *day = 0; if (total) *total = 0;
    if (cur) *cur = PHASE_OFF; if (next) *next = -1; if (next_in) *next_in = 0;
    if (ch < 0 || ch >= N_CHAMBERS) return;
    int tot = 0;
    for (int i = 0; i < GROWPLAN_STEPS; i++) { if (s_growplan[ch][i].days == 0) break; tot += s_growplan[ch][i].days; }
    if (total) *total = tot;
    if (!s_plan_en[ch] || s_grow_start[ch] == 0) return;
    time_t now = time(NULL);
    int d0 = (now < (time_t)s_grow_start[ch]) ? 0 : (int)((now - (time_t)s_grow_start[ch]) / 86400);  // 0-basiert
    if (day) *day = d0 + 1;
    if (cur) *cur = planned_phase(ch);
    int acc = 0;
    for (int i = 0; i < GROWPLAN_STEPS; i++) {
        if (s_growplan[ch][i].days == 0) break;
        int end = acc + s_growplan[ch][i].days;
        if (d0 < end) {
            if (next_in) *next_in = end - d0;
            if (i + 1 < GROWPLAN_STEPS && s_growplan[ch][i + 1].days > 0 && next) *next = s_growplan[ch][i + 1].phase;
            return;
        }
        acc = end;
    }
    // jenseits des Plan-Endes: keine nächste Phase
}

// Plan-Phase aus (heute − Grow-Start) gegen die Schritt-Dauern. Vor Start → erster Schritt;
// nach Plan-Ende → letzte definierte Phase halten (z.B. Trocknen, wenn als letzter Schritt).
static grow_phase_t planned_phase(int ch)
{
    if (s_grow_start[ch] == 0) return s_phase[ch];
    time_t now = time(NULL);
    if (now < (time_t)s_grow_start[ch]) return (grow_phase_t)s_growplan[ch][0].phase;
    int day = (int)((now - (time_t)s_grow_start[ch]) / 86400);   // 0-basiert
    int acc = 0; grow_phase_t last = s_phase[ch];
    for (int i = 0; i < GROWPLAN_STEPS; i++) {
        if (s_growplan[ch][i].days == 0) break;
        last = (grow_phase_t)s_growplan[ch][i].phase;
        acc += s_growplan[ch][i].days;
        if (day < acc) return last;
    }
    return last;
}

bool climate_plan_enabled(int ch) { return (ch >= 0 && ch < N_CHAMBERS) ? s_plan_en[ch] != 0 : false; }

void climate_set_plan_enabled(int ch, bool on)
{
    if (ch >= 0 && ch < N_CHAMBERS) s_plan_en[ch] = on ? 1 : 0;
}

void climate_get_growplan(int ch, growplan_step_t out[GROWPLAN_STEPS])
{
    if (ch < 0 || ch >= N_CHAMBERS) { memset(out, 0, sizeof(growplan_step_t) * GROWPLAN_STEPS); return; }
    memcpy(out, s_growplan[ch], sizeof(growplan_step_t) * GROWPLAN_STEPS);
}

void climate_set_growplan(int ch, const growplan_step_t in[GROWPLAN_STEPS])
{
    if (ch < 0 || ch >= N_CHAMBERS) return;
    for (int i = 0; i < GROWPLAN_STEPS; i++) {
        growplan_step_t s = in[i];
        if (s.phase >= PHASE_MAX) s.phase = 0;
        if (s.days > 365) s.days = 365;
        s_growplan[ch][i] = s;
    }
}

void climate_save_growplan(void)
{
    nvsu_save("grow", "plan_en", s_plan_en, sizeof(s_plan_en));
    nvsu_save("grow", "af_mode", s_af_mode, sizeof(s_af_mode));
    nvsu_save("grow", "af_lon",  s_af_lon,  sizeof(s_af_lon));
    nvsu_save("grow", "af_lst",  s_af_lstart, sizeof(s_af_lstart));
    if (nvsu_save("grow", "growplan", s_growplan, sizeof(s_growplan)))
        ESP_LOGI(TAG, "Grow-Plan gespeichert");
}

int climate_plan_day(int ch)
{
    if (ch < 0 || ch >= N_CHAMBERS || !s_plan_en[ch] || s_grow_start[ch] == 0) return 0;
    time_t now = time(NULL);
    if (now < (time_t)s_grow_start[ch]) return 0;
    return (int)((now - (time_t)s_grow_start[ch]) / 86400) + 1;   // 1-basiert
}

void climate_get_grow(int ch, uint32_t *start_epoch, uint16_t *total_days)
{
    if (ch < 0 || ch >= N_CHAMBERS) ch = 0;
    if (start_epoch) *start_epoch = s_grow_start[ch];
    if (total_days)  *total_days  = s_grow_days[ch];
}

void climate_set_grow(int ch, uint32_t start_epoch, uint16_t total_days)
{
    if (ch < 0 || ch >= N_CHAMBERS) return;
    s_grow_start[ch] = start_epoch;
    if (total_days > 0) s_grow_days[ch] = total_days;
    bool ok = nvsu_set_u32("grow", ch == 0 ? "gstart0" : "gstart1", s_grow_start[ch]);
    ok &= nvsu_set_u16("grow", ch == 0 ? "gdays0" : "gdays1", s_grow_days[ch]);
    if (ok) ESP_LOGI(TAG, "Grow Kammer %c: start=%lu, %u Tage", 'A' + ch,
                     (unsigned long)s_grow_start[ch], s_grow_days[ch]);
}

esp_err_t climate_config_init(void)
{
    load_phase_nvs();
    climate_load_setpoints();      // überschreibt Defaults falls in NVS vorhanden
    load_global_nvs();             // globale Config (Buzzer/Lüfter/Lockout)
    load_grow_nvs();               // Grow-Start/Dauer (Ernte-Timer)
    manual_load();                 // manuelle Overrides iFan/Humid/Clip (überleben Reboot)
    nvsu_load_partial("grow", "dlitgt", s_dli_tgt, sizeof(s_dli_tgt), NULL);  // DLI-Ziele
    nvsu_load_partial("grow", "lmode",   s_light_mode, sizeof(s_light_mode), NULL); // %/PPFD
    nvsu_load_partial("grow", "ppfdtgt", s_ppfd_tgt,   sizeof(s_ppfd_tgt),   NULL); // PPFD-Ziele
    nvsu_load_partial("grow", "tempoff", s_temp_off,   sizeof(s_temp_off),   NULL); // Temp-Offsets
    nvsu_load_partial("grow", "rhoff",   s_rh_off,     sizeof(s_rh_off),     NULL); // rH-Offsets
    return ESP_OK;
}

void climate_set_phase(int chamber, grow_phase_t p)
{
    if (chamber < 0 || chamber >= N_CHAMBERS || p > PHASE_OFF) return;
    s_phase[chamber] = p;
    nvsu_set_u8("grow", chamber == 0 ? "ph0" : "ph1", p);
    ESP_LOGI(TAG, "Kammer %c Phase → %s", 'A' + chamber, climate_phase_name(p));
}

grow_phase_t climate_get_phase(int chamber)
{
    if (chamber < 0 || chamber >= N_CHAMBERS) return PHASE_OFF;
    // Grow-Plan hat Vorrang, wenn aktiv UND ein Grow-Start gesetzt ist.
    if (s_plan_en[chamber] && s_grow_start[chamber] != 0) return planned_phase(chamber);
    return s_phase[chamber];
}

void climate_set_chamber_auto(int chamber, bool on)
{
    if (chamber < 0 || chamber >= N_CHAMBERS) return;
    s_chamber_auto[chamber] = on;
    nvsu_set_u8("grow", chamber == 0 ? "auto0" : "auto1", on ? 1 : 0);
    ESP_LOGI(TAG, "Kammer %c Automatik → %s", 'A' + chamber, on ? "AN" : "AUS");
}

bool climate_get_chamber_auto(int chamber)
{
    return (chamber >= 0 && chamber < N_CHAMBERS) ? s_chamber_auto[chamber] : false;
}

void climate_set_sensor(int chamber, sensor_src_t src, const uint8_t mac[6])
{
    if (chamber < 0 || chamber >= N_SENS) return;   // inkl. Dachboden-Slot
    s_csens[chamber].src = (uint8_t)src;
    if (mac) memcpy(s_csens[chamber].mac, mac, 6);
    nvsu_save("grow", "csens", s_csens, sizeof(s_csens));
    ESP_LOGI(TAG, "Kammer %c Sensor-Quelle → %d", 'A' + chamber, src);
}

void climate_get_sensor(int chamber, sensor_src_t *src, uint8_t mac[6])
{
    if (chamber < 0 || chamber >= N_SENS) return;   // inkl. Dachboden-Slot
    if (src) *src = (sensor_src_t)s_csens[chamber].src;
    if (mac) memcpy(mac, s_csens[chamber].mac, 6);
}

const char  *climate_phase_name(grow_phase_t p) { return p == PHASE_OFF ? "Aus" : (p < PHASE_MAX ? k_names[p] : "?"); }
phase_setpoints_t *climate_setpoints(grow_phase_t p) { return p < PHASE_MAX ? &sp[p] : NULL; }
