// devices.h — High-Level-Zugriff auf die RJ12-Modbus-Geräte des iHub-Pro
//
// Slave-Map + Register-Positionen aus docs/MODBUS_REGISTERS.md
// (per Bus-Sniffing 2026-05-31/06-01 ermittelt).
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// ── Slave-Adressen (Sensor-Bus) ──
// Geräte-Typ-IDs (Reg1 der Antwort, = 0xBAxx) aus dem Original-FW-Dump verifiziert
// (2026-06-03): jedes Modul meldet seinen Typ in Reg1. Siehe docs/MODBUS_REGISTERS.md.
#define SLAVE_TH3IN1      0x02   // Klima-Sensor Growkammer  (Typ 0xBA03 "MH-SH")
#define SLAVE_TH3IN1_ALT  0x0F   // Ausweich-Adresse: manche Exemplare wurden auf 0x0F umadressiert
#define SLAVE_TH3IN1_2    0x04   // 2. Klima-Sensor Dachboden/Quellluft (umadressiert)
#define SLAVE_CO2         0x03   // (Typ 0xBA09 "MH-CO2")
#define SLAVE_PAR         0x09   // (Typ 0xBA06 "MH-PPFD")
#define SLAVE_IFAN        0x06   // (Typ 0xBA06 "MH-iFan")
#define SLAVE_HUMIDIFIER  0x0E   // (Typ 0xBA0E "MH-HUMIDIFIER")
#define SLAVE_SUBSTRATE   0x01   // per Bus-Scan ermittelt (war fälschlich 0x14); Typ 0xBA01/02 "MH-STHE"
// Per FW-Dump zugeordnet, Hardware noch nicht angeschlossen → noch nicht in devices_poll verdrahtet:
#define SLAVE_AE          0x04   // Air-Exchange/Lüfter (Typ 0xBA04): onoff,adc_voltage,pwm_duty,fg_freq
#define SLAVE_CDH         0x0D   // Kombi-Entfeuchter/Heizer (Typ 0xBA0D): onoff,mode,fan_level,ecode
// Clip-Fan läuft als Broadcast (Slave 0x00, Func 0x10) auf dem Fan-Bus.

// Aktuelle Sensorwerte (skaliert in physikalische Einheiten).
typedef struct {
    bool  th3in1_valid;
    float temperature_c;     // TH3in1 Reg10 / 10  (Growkammer)
    float humidity_pct;      // TH3in1 Reg11 / 10
    float light_lux;         // TH3in1 Reg12 (1:1)

    bool  attic_valid;       // Fan-Bus-TH3in1 ROHWERT (Slave 0x02 am Fan-Bus) — von devices_poll
    float attic_temp_c;
    float attic_humidity_pct;

    bool  ref_valid;         // Dachboden/Quellluft-REFERENZ (Free-Cooling) — konfigurierbare Quelle
    float ref_temp_c;        // (Fan-Bus / BLE / Sensor-Bus, gesetzt in climate apply_attic_source)
    float ref_humidity_pct;  // NICHT von devices_poll überschreiben!

    bool  co2_valid;
    int   co2_ppm;           // CO2 = Reg10 + Reg12 (Hypothese, s. docs/MODBUS_REGISTERS.md)
    int   co2_raw;           // Debug: Reg10 (dynamischer Mess-Anteil)
    int   co2_base;          // Debug: Reg12 (konstant ~472, vermutete Baseline)

    bool  par_valid;
    int   ppfd;              // PAR Reg11 (1:1)

    bool  substrate_valid;   // Feuchte + EC gültig
    bool  soil_temp_valid;   // Substrat-Temp gültig (false wenn Fühler-Temp defekt/festhängend)
    float soil_temp_c;       // Substrat Reg11 / 10
    float soil_moist_pct;    // Substrat Reg12 / 10
    float soil_ec;           // Substrat Reg13 / 100

    bool  ifan_valid;
    int   ifan_speed_pct;    // iFan Reg16 (0-100)
} sensor_data_t;

// Initialisiert beide Modbus-Busse.
esp_err_t devices_init(void);

// Pollt alle angeschlossenen Sensoren einmal, füllt `out`.
// (Nicht-antwortende Slaves → *_valid = false.)
esp_err_t devices_poll(sensor_data_t *out);

// ── Aktoren ──
// Humidifier (Slave 0x0E): Stufe 0..4. Setzt Power(Reg12)+Stufe(Reg14).
esp_err_t humidifier_set(int level_0_4);

// iFan (Slave 0x06): Speed 0..100 % (Reg 0x10).
esp_err_t ifan_set(int pct);

// Clip-Fan (Fan-Bus Broadcast 0x10, Reg 0x0A..0x0D):
// stufe 0-10, schwenk 0/5/10 (=aus/45/90°), natural_wind 0/1.
esp_err_t clipfan_set(int stufe, int schwenk, bool natural_wind);
esp_err_t clipfan_raw4(int bus, int startreg, int a, int b, int c, int d);  // DEBUG: Schwenk-RE (bus 0=Sensor,1=Fan)
esp_err_t fan_bcast1001(int bus, int r1009, int r1011, int r1012);          // DEBUG: OEM-Aktor-Broadcast Reg1001
esp_err_t fan_bcast1001_pos(int bus, int pos, int val);                     // DEBUG: Reg1001 mit einer gesetzten Position
int fan_raw(int bus, int baud, const uint8_t *payload, int len, uint8_t *resp, int resp_max); // DEBUG: Roh-Frame @ Baud

// Berechnet VPD (kPa) aus Temp + rH.
float vpd_kpa(float temp_c, float rh_pct);

// Absolute Feuchte (g/m³) aus Temp + rH — für den Luftraum-Vergleich (Free Cooling).
float abs_humidity_gm3(float temp_c, float rh_pct);

// Diagnose: Sensor-Bus nach antwortenden Modbus-Slaves (Adressen from..to)
// absuchen. Schreibt "0xAA:r0 r1 ... | 0xBB:..." nach out. Rückgabe = Anzahl.
int devices_mb_scan(uint8_t bus, uint8_t from, uint8_t to, uint16_t count, uint8_t func, char *out, int outlen);

// Diagnose: einzelnes Holding-Register schreiben (Func 0x06) auf dem Sensor-Bus.
esp_err_t devices_mb_write(uint8_t slave, uint16_t reg, uint16_t val);
