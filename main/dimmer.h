// dimmer.h — 0-10V Dimmer für LIGHT1/LIGHT2 via LEDC-PWM + BL358-OpAmp
//
// ESP32 PWM (GPIO35/37) → RC-Filter → BL358 (U6) → 0-10V an V51 LIGHT1/2.
// Pro Kanal invertierbar: manche LED-Treiber sind 0V=hell/10V=aus (invertiert),
// andere 0V=aus/10V=hell (normal). Konfigurierbar + in NVS persistiert.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum { DIM_LIGHT1 = 0, DIM_LIGHT2, DIM_COUNT } dim_ch_t;

// Initialisiert LEDC + lädt invert-Flags aus NVS (Start 0 %).
esp_err_t dimmer_init(void);

// Setzt Helligkeit 0..100 % (invert-Flag wird angewendet).
esp_err_t dimmer_set(dim_ch_t ch, uint8_t pct);
// Wie dimmer_set, persistiert den Wert aber in NVS (überlebt Reboot). Nur für manuelle
// Befehle (WebUI/MQTT) — NICHT aus der Regelschleife aufrufen (Flash-Wear).
esp_err_t dimmer_set_manual(dim_ch_t ch, uint8_t pct);
uint8_t   dimmer_get(dim_ch_t ch);

// Invertierung pro Kanal setzen/lesen (wird in NVS gespeichert).
void      dimmer_set_invert(dim_ch_t ch, bool inverted);
bool      dimmer_get_invert(dim_ch_t ch);

// Nullpunkt-Kalibrierung pro Kanal: Ausgang in % bei Slider 0 (Default 7). NVS-persistiert.
void      dimmer_set_cal(dim_ch_t ch, uint8_t min_pct);
uint8_t   dimmer_get_cal(dim_ch_t ch);

// 10-Stufen-Slider (leistungs-linear kalibriert): Stufe 0..10 → interner PWM-% via Lookup.
esp_err_t dimmer_set_step(dim_ch_t ch, uint8_t step);
uint8_t   dimmer_get_step(dim_ch_t ch);   // aktuelle Stufe (aus PWM-% rückgerechnet)
