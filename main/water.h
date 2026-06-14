// water.h — Wasserwerte des RDWC-Systems (pH/EC/TDS/ORP/Temp/…).
// Der iHub MISST diese nicht selbst — sie kommen vom Tuya-8-in-1-Tester über
// Home Assistant (tuya-local) per MQTT (Topics grow/water/<key>, retained) und
// werden hier nur empfangen, angezeigt und überwacht.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float ph;             bool ph_valid;
    float ec_us;          bool ec_valid;        // µS/cm
    float tds_ppm;        bool tds_valid;       // ppm
    float orp_mv;         bool orp_valid;       // mV
    float temp_c;         bool temp_valid;      // Wassertemperatur °C
    float salinity_ppm;   bool salinity_valid;  // ppm
    float sg;             bool sg_valid;        // spezifisches Gewicht
    float cf;             bool cf_valid;        // Conductivity factor
    int64_t last_update_us;   // esp_timer_get_time() beim letzten Empfang (Stale-Erkennung)
} water_data_t;

// true, wenn zuletzt vor < max_age_s Sekunden ein Wert empfangen wurde.
// (HA aus / Tester offline → nach Ablauf gelten die Werte als veraltet.)
bool water_is_fresh(const water_data_t *w, int max_age_s);

// Einen per MQTT empfangenen Wert (Topic-Suffix nach grow/water/) in das Struct
// übernehmen. Payload-Strings "unavailable"/"unknown"/leer → das jeweilige
// *_valid wird false (Wert nicht angetastet). Setzt last_update_us bei gültiger Zahl.
// Gibt true zurück, wenn der key erkannt wurde.
bool water_apply_kv(water_data_t *w, const char *key, const char *payload);
