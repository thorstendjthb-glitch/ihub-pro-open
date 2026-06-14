// climate_control.h — Cannabis-Grow-Regelung: Phasen, VPD/Temp, Photoperiode, DLI
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "relays.h"   // N_CHAMBERS

// Sensor-Slots: 0/1 = Kammer A/B, CSENS_ATTIC = Dachboden/Quellluft (Free-Cooling-Referenz).
#define CSENS_ATTIC N_CHAMBERS
#define N_SENS      (N_CHAMBERS + 1)

// Grow-Profile (vom Anwender wählbar). Jedes Profil hat eigene Sollwerte.
typedef enum {
    PHASE_SEEDS = 0,     // Samen/Keimung     18/6, hohe rH, niedriger VPD
    PHASE_CLONES,        // Stecklinge        18/6, sehr hohe rH
    PHASE_VEG,           // Wuchs (vegetativ) 18/6
    PHASE_FLOWER,        // Blüte             12/12
    PHASE_AUTO,          // Automatics        20/4 durchgehend (photoperioden-unabhängig)
    PHASE_DRY,           // Trocknen (nach Ernte, kein Licht)
    PHASE_MAX,           // Anzahl Profile MIT Sollwerten (Array-Größe)
    PHASE_OFF = PHASE_MAX // "Aus": Automatik komplett deaktiviert (kein Setpoint)
} grow_phase_t;

// Sollwerte je Phase (Tag/Nacht). Aus NVS ladbar/editierbar.
typedef struct {
    uint8_t  light_on_h;     // Lichtzyklus: Stunden AN pro Tag (18 oder 12)
    uint8_t  light_start_h;  // Zeitschaltuhr: Uhrzeit Licht-AN (Stunde 0-23)
    float    vpd_target;     // Ziel-VPD kPa
    float    vpd_deadband;   // Hysterese kPa
    float    temp_day;       // °C
    float    temp_night;     // °C
    float    temp_deadband;  // °C
    float    rh_day;         // %
    float    rh_night;       // %
    uint16_t co2_target;     // ppm (0 = aus)
    uint8_t  co2_only_daylight; // 1 = CO2 nur während Lichtphase (Pflanze nutzt CO2 nur bei Licht)
    uint8_t  light_pct;      // Lampen-Helligkeit % (Plateau)
    uint16_t ramp_min;       // Sonnenauf-/untergang-Dauer in Minuten (0 = aus, hart schalten)
    float    temp_alarm;     // Übertemp-Alarm-Schwelle absolut °C (0 = Alarm aus)
    float    rh_alarm;       // Feuchte-/Schimmel-Alarm-Schwelle % (0 = Alarm aus)
    float    temp_min_alarm; // Untertemp-Alarm-Schwelle °C (0 = Alarm aus)
    uint16_t co2_max_alarm;  // CO2-zu-hoch-Alarm ppm (0 = Alarm aus)
} phase_setpoints_t;

// Globale Einstellungen (profil-übergreifend), in NVS persistiert.
typedef struct {
    uint8_t  buzzer_enable;   // 1 = akustischer Alarm-Buzzer aktiv
    uint16_t alarm_repeat_s;  // Wiederhol-Intervall Buzzer bei Daueralarm (s)
    uint16_t fan_min_cycle_s; // Abluft-Lüfter Anti-Takt: Mindestlaufzeit vor Aus (s)
    uint8_t  lockout_dim;     // 1 = bei Übertemp-Alarm Lampen auf 50% drosseln
    uint8_t  lockout_fan;     // 1 = bei Übertemp-Alarm Abluft erzwingen
    uint8_t  sensor_alarm_en; // 1 = Alarm wenn Klimasensor (TH3in1) ausfällt
    uint8_t  fan_base;        // Abluft-Grundlast % (Dauerzug für Geruch/Aktivkohle)
    uint8_t  fan_max;         // Abluft-Maximaldrehzahl %
    uint16_t light_fault_lux; // Lampen-Ausfall-Kontrolle: Mindest-Lux am TH3in1-Lichtsensor,
                              // wenn Licht laut Zeitplan AN sein soll. 0 = Überwachung aus.
    uint16_t water_max_min;   // Bewässerung: max. Dauerlaufzeit in Minuten, danach
                              // Sicherheitsabschaltung (Überflutungsschutz). 0 = aus.
    uint16_t hist_save_min;   // Verlauf+DLI alle X Minuten in den Flash sichern
                              // (überlebt Reboot/Update). 0 = Persistenz aus.
    uint8_t  dli_floor_pct;   // DLI-Zielregelung: Helligkeit nach Ziel-Erreichen auf
                              // diesen %-Wert drosseln (Photoperiode bleibt!).
    // ── Wasserwert-Alarme (RDWC; Werte kommen von HA via MQTT, siehe water.h) ──
    uint8_t  water_alarm_en;  // 1 = Wasserwert-Alarme aktiv (Default 0 → kein Fehlalarm bei Testwasser)
    float    water_ph_min;    // pH-Untergrenze (0 = aus)
    float    water_ph_max;    // pH-Obergrenze (0 = aus)
    float    water_temp_max;  // Wassertemp-Obergrenze °C (0 = aus)
    float    water_temp_min;  // Wassertemp-Untergrenze °C (0 = aus)
    int16_t  water_orp_min;   // ORP-Untergrenze mV (0 = aus; int, da ORP negativ sein kann)
    // ── Klimaanlage: Verzögerung vor AC-Anforderung (Free-Cooling-Vorrang) ──
    uint8_t  ac_delay_min;    // Beobachtungsfenster (min): bringt die Abluft in dieser Zeit KEINE
                              // messbare Kühlung/Trocknung (Temp sinkt nicht / VPD steigt nicht),
                              // wird die Klimaanlage angefordert. 0 = sofort. Erfasst auch defekte/
                              // ausgeschaltete Abluft (echtes Temp-Feedback, nicht nur Dachboden-Δ).
    // ── CO2-Frischluft-Baseline (FRC) ──
    uint16_t co2_baseline;    // CO2 = Reg10 + co2_baseline (ppm). Frischluft-Kalibrierung:
                              // bei frischer Außenluft soll der Sensor ~420 anzeigen. 0 = Default 420.
                              // (Original-FW nimmt Reg10 ohne Baseline → unphysikalisch ~0; s. MODBUS_REGISTERS.md)
    int8_t   ac_chamber;      // Kammer mit Klimaanlage: 0 = A (Default), 1 = B, -1 = keine AC.
                              // Nur diese Kammer fordert AC an (ac_demand) und drosselt bei AC-
                              // Betrieb ihre Abluft. Default 0 = zugleich der NVS-Padding-Wert.
    uint8_t  par_chamber;     // Kammer mit dem (einen) PAR/PPFD-Sensor: 0 = A (Default), 1 = B.
                              // Nur dort greifen PPFD-Lichtregelung + DLI-Drossel; die andere
                              // Kammer nutzt feste %-Helligkeit (kein Regeln gegen Fremdmesswert).
                              // NUR AM ENDE ERWEITERN — alte NVS-Blobs laden partiell.
} grow_global_t;

typedef struct {
    grow_phase_t phase;
    bool   is_day;           // aus Photoperiode
    float  vpd_now;
    float  dli_today;        // mol/m²/Tag (integriert)
    bool   alarm_mold;       // Schimmel-Warnung aktiv (rH zu hoch)
    bool   alarm_temp;       // Temperatur-Alarm aktiv (über ODER unter Schwelle)
    bool   alarm_co2;        // CO2-zu-hoch-Alarm aktiv
    bool   alarm_sensor;     // Klimasensor-Ausfall-Alarm aktiv
    bool   alarm_light;      // Lampen-Ausfall: Licht soll AN sein, Lichtsensor meldet dunkel
    bool   alarm_time;       // Systemzeit ungültig (SNTP+RTC aus) → Photoperiode angehalten
    bool   alarm_water;      // Wasserwert (pH/Temp/ORP) außerhalb Korridor (von HA via MQTT)
    uint8_t ifan_pct;        // aktuell angesteuerte Abluft-Drehzahl %
    bool    ac_demand;       // Klimaanlage angefordert (Free-Cooling reicht nicht)
    uint8_t ac_mode;         // 0=aus 1=kühlen 2=entfeuchten 3=beides
    uint32_t light_on_secs;  // wann Licht heute anging (Sek seit Mitternacht)
} climate_status_t;

// ── 2 Kammern: Sensor-Quelle + Live-Zustand je Kammer ──
typedef enum { SRC_TH_SENSOR = 0, SRC_TH_FAN = 1, SRC_BLE = 2 } sensor_src_t;

typedef struct {
    uint8_t phase;
    bool    is_day, valid;     // valid = Klimasensor liefert Werte
    float   temp, rh, vpd;
    uint8_t ifan_pct;
    bool    alarm_temp, alarm_mold;
    bool    alarm_light;       // Lampen-Ausfall-Verdacht (Licht-Soll-AN, aber dunkel)
    float   light_lux;         // gemessener Lichtwert (nur wenn Kammer-Sensor = TH3in1)
} chamber_state_t;

esp_err_t climate_init(void);
void      climate_start(void);                       // Regel-Task starten
void      climate_set_phase(int chamber, grow_phase_t p);   // Phase je Kammer (persistiert)
grow_phase_t climate_get_phase(int chamber);
void      climate_set_chamber_auto(int chamber, bool on);   // Klima-Automatik je Kammer (persistiert)
bool      climate_get_chamber_auto(int chamber);
void      climate_chamber(int chamber, chamber_state_t *out);
void      climate_set_sensor(int chamber, sensor_src_t src, const uint8_t mac[6]);
void      climate_get_sensor(int chamber, sensor_src_t *src, uint8_t mac[6]);

// Abluft-iFan manuell übersteuern: pct 0..100 = fester Wert, <0 = zurück auf Automatik.
void      climate_set_ifan_manual(int pct);
int       climate_get_ifan_manual(void);   // -1 = Automatik, sonst manueller %-Wert

// Luftbefeuchter manuell übersteuern: Stufe 0..4 = fest, <0 = zurück auf Automatik (VPD).
void      climate_set_humid_manual(int level);
int       climate_get_humid_manual(void);  // -1 = Automatik, sonst Stufe 0..4

// Clip-Fan (Direktsteuerung, nicht geregelt): Stufe 0..10, Schwenk 0/5/10, Natural-Wind.
void      climate_set_clipfan(int stufe, int schwenk, bool natural);
void      climate_get_clipfan(int *stufe, int *schwenk, bool *natural);

// Akustischen Alarm quittieren (stummschalten); Visual bleibt bis Ursache weg.
// Wird bei neuem/erneutem Alarm automatisch zurückgesetzt.
void climate_alarm_ack(void);
void      climate_get_status(climate_status_t *out);
const char *climate_phase_name(grow_phase_t p);
phase_setpoints_t *climate_setpoints(grow_phase_t p);  // editierbar

// Sollwerte aller Phasen in NVS speichern / daraus laden.
void climate_save_setpoints(void);
void climate_load_setpoints(void);

// DLI-Tagesziel je Phase in mol/m² (0 = Zielregelung aus). Bewusst SEPARATER
// NVS-Blob (nicht in phase_setpoints_t!) — eine Struct-Erweiterung würde wegen der
// exakten Größenprüfung alle getunten Sollwerte auf Defaults zurückwerfen.
// Setter ändert nur RAM; einmal climate_save_dli_targets() persistiert alle.
float climate_dli_target(grow_phase_t p);
void  climate_set_dli_target(grow_phase_t p, float mol);
void  climate_save_dli_targets(void);

// PPFD-Lichtregelung je Phase (separate NVS-Blobs, gleiche Begründung wie DLI):
// light_mode 0 = feste Helligkeit (light_pct), 1 = PPFD-Zielregelung auf ppfd_target.
uint8_t  climate_light_mode(grow_phase_t p);
uint16_t climate_ppfd_target(grow_phase_t p);
void     climate_set_light_mode(grow_phase_t p, uint8_t mode);
void     climate_set_ppfd_target(grow_phase_t p, uint16_t ppfd);
void     climate_save_light_ppfd(void);

// Sensor-Kalibrier-Offsets je Slot (0/1 = Kammer A/B, CSENS_ATTIC = Dachboden), additiv vor VPD.
float climate_temp_offset(int slot);
float climate_rh_offset(int slot);
void  climate_set_offsets(int slot, float temp_off, float rh_off);
void  climate_save_offsets(void);

// Alarm-Ereignisprotokoll (Ringpuffer): jede Alarm-Flanke (an/aus) mit Zeitstempel.
// type: 0=temp 1=mold 2=co2 3=sensor 4=light 5=water. Für Diagnose/Nachvollziehbarkeit.
typedef struct { uint32_t ts; uint8_t type; uint8_t on; } alarm_evt_t;
int climate_alarmlog(alarm_evt_t *out, int max);   // neueste zuerst, Rückgabe = Anzahl

// Globale Einstellungen (editierbar + persistierbar).
grow_global_t *climate_global(void);
void climate_save_global(void);

// Grow-Zyklus für Ernte-Timer/Kalender: Startzeitpunkt (Unix-Epoch, 0=unbekannt)
// + erwartete Gesamtdauer in Tagen. In NVS persistiert.
void climate_get_grow(int chamber, uint32_t *start_epoch, uint16_t *total_days);
void climate_set_grow(int chamber, uint32_t start_epoch, uint16_t total_days);

// ── Grow-Plan: zeitgesteuerte Phasenabfolge je Kammer ──────────────────────────
// Ist der Plan aktiv UND ein Grow-Start gesetzt, berechnet climate_get_phase() die
// Phase automatisch aus (heute − Grow-Start) gegen die Schritt-Dauern. Sonst gilt
// die manuell gesetzte Phase. Separate NVS-Blobs (ändern phase_setpoints_t nicht).
#define GROWPLAN_STEPS 6
typedef struct {
    uint8_t  phase;   // grow_phase_t (0..PHASE_MAX-1); ungenutzte Schritte: days=0
    uint16_t days;    // Dauer dieses Schritts in Tagen (0 = Schritt ungenutzt/Plan-Ende)
} growplan_step_t;

bool climate_plan_enabled(int chamber);
void climate_set_plan_enabled(int chamber, bool on);
void climate_get_growplan(int chamber, growplan_step_t out[GROWPLAN_STEPS]);
void climate_set_growplan(int chamber, const growplan_step_t in[GROWPLAN_STEPS]);
void climate_save_growplan(void);
// Aktueller Plan-Tag (1-basiert, 0 = kein Start/Plan inaktiv) — für Status/Anzeige.
int  climate_plan_day(int chamber);
// Plan-Übersicht für Dashboard: day (1-basiert, 0=inaktiv), total (Summe Schritt-Tage),
// cur (aktuelle Phase), next (nächste Phase oder -1), next_in (Tage bis Phasenwechsel).
void climate_plan_info(int chamber, int *day, int *total, int *cur, int *next, int *next_in);

// ── Autoflower-Modus (Photoperiode-Lock) je Kammer ──
// Ist er aktiv, gilt für ALLE Phasen ein fester Lichtplan (light_on/start) statt des
// phasen-eigenen — Klima/VPD/CO2/PPFD wechseln per Grow-Plan, das Licht bleibt konstant.
bool    climate_af_mode(int chamber);
uint8_t climate_af_light_on(int chamber);     // Licht-Stunden/Tag (z.B. 20)
uint8_t climate_af_light_start(int chamber);  // Start-Stunde 0..23
void    climate_set_af(int chamber, uint8_t mode, uint8_t light_on, uint8_t light_start);
