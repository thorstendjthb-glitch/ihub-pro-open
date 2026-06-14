// history.h — On-Device-Verlaufsspeicher (Ring-Puffer im RAM)
#pragma once
#include <time.h>
#include <stdint.h>
#include "devices.h"
#include "bl0940.h"
#include "climate_control.h"

#define HIST_N           2016   // Kurzzeit-Ring: Stützpunkte
#define HIST_INTERVAL_S  300    // 5 min → 2016×5min = 7 Tage (fein, für Details)
#define HIST_LONG_N      2880   // Langzeit-Ring: Stützpunkte
#define HIST_LONG_INT_S  3600   // 1 h → 2880×1h = 120 Tage (grob, ganzer Grow); im PSRAM

// Pro Kammer A/B je Temp/rH/VPD + globale CO₂/Leistung/Abluft + 7 RDWC-Wasserwerte.
typedef struct {
    int16_t a_temp, a_rh, a_vpd;
    int16_t b_temp, b_rh, b_vpd;
    int16_t co2, power, ifan;
    int16_t w_ph, w_orp, w_temp, w_ec, w_tds, w_sal, w_sg;   // Wasserwerte (skaliert)
} hist_sample_t;

#define HIST_METRICS 16  // Anzahl abfragbarer Kanäle (siehe history_get)

// Aus dem Regel-Task aufrufen; speichert alle HIST_INTERVAL_S einen Sample.
void history_tick(time_t now, const sensor_data_t *d, const power_data_t *pw, const climate_status_t *st);

// metric: 0=A-Temp 1=A-rH 2=A-VPD 3=B-Temp 4=B-rH 5=B-VPD 6=CO₂ 7=Leistung 8=Abluft
//         9=Wasser-pH 10=Wasser-ORP 11=Wassertemp 12=Wasser-EC 13=Wasser-TDS
//         14=Wasser-Salinität 15=Wasser-Dichte(SG).
// Füllt out[] (ältester→neuester), setzt *interval + *newest. Rückgabe = Anzahl Werte.
// longterm=false → 7-Tage-Ring (5 min); longterm=true → Langzeit-Ring (1 h, ~120 Tage).
int history_get(int metric, int16_t *out, int maxn, int *interval, uint32_t *newest, bool longterm);

// ── Persistenz: Ring-Puffer + DLI in die Datenpartition „spiffs" (Rohspeicher) ──
// Zwei alternierende 64-KB-Slots mit Sequenznummer + CRC32 → stromausfallsicher,
// halber Flash-Verschleiß. Bei Standard-Intervall 60 min: ~24 Erase-Zyklen/Tag
// auf 2 Slots → Jahrzehnte unter den ~100k-Zyklen des Flash.

// Beim Boot aufrufen (vor dem ersten history_tick): stellt Puffer wieder her und
// liefert gesichertes DLI (mol/m²) + Jahrestag. false = nichts/ungültig gespeichert.
bool history_restore(float *dli_today, int *yday);

// Im Regel-Task aufrufen; sichert alle save_min Minuten (0 = Persistenz aus).
// Erste Sicherung erst ein volles Intervall nach dem Boot (spart einen Zyklus).
void history_persist_tick(time_t now, uint16_t save_min, float dli_today, int yday);
