// relays.h — Steuerung der 10 AC-Relais des iHub-Pro
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Logische Relais-Indizes (Reihenfolge wie Front-Beschriftung)
typedef enum {
    RLY_LIGHT1 = 0,
    RLY_LIGHT2,
    RLY_HUMIDIFIER,
    RLY_DEHUMID,
    RLY_WATERING,
    RLY_FAN,
    RLY_INLINEFAN,
    RLY_HEATING,
    RLY_DEVICE1,
    RLY_DEVICE2,
    RLY_MAX
} relay_id_t;

// Modus pro Relais: AUTO = Klimaregelung steuert; MANUAL = User-Override.
typedef enum { RMODE_AUTO = 0, RMODE_MANUAL } relay_mode_t;

// Initialisiert alle Relais-GPIOs als Output, Startzustand AUS, Modus AUTO.
esp_err_t relays_init(void);

// Manuelles Schalten (vom WebUI/MQTT): setzt Modus MANUAL + Zustand.
// Die Klimaregelung lässt MANUAL-Relais danach in Ruhe.
esp_err_t relay_set_manual(relay_id_t id, bool on);

// Automatisches Schalten (von der Klimaregelung/Bewässerung): wirkt NUR wenn
// Modus == AUTO und KEIN Zeitplan die Steckdose beansprucht (relay_sched_claim).
esp_err_t relay_set_auto(relay_id_t id, bool on);

// ── Zeitplan-Ebene (relay_sched.c) — Priorität MANUELL > Zeitplan > Regelung ──
// Claim: Steckdose dem Zeitplan zuordnen → relay_set_auto wird dort zum No-Op.
void relay_sched_claim(relay_id_t id, bool claimed);
// Schalten durch den Zeitplan: wirkt NUR wenn Modus == AUTO (Manuell gewinnt).
esp_err_t relay_set_sched(relay_id_t id, bool on);

// Modus setzen/lesen. relay_set_mode(id, RMODE_AUTO) gibt den Kanal an die
// Regelung zurück.
void         relay_set_mode(relay_id_t id, relay_mode_t mode);
relay_mode_t relay_get_mode(relay_id_t id);

bool relay_get(relay_id_t id);
const char *relay_name(relay_id_t id);

// ── Funktions-Ebene (frei zuweisbare Aktor-Zuordnung) ──
// Die Klimaregelung steuert logische FUNKTIONEN; eine Tabelle bildet jede
// Funktion auf eine physische Steckdose (0..9) ab, oder -1 = nicht belegt.
// Default = Identität (Funktion i → Steckdose i). In NVS persistiert.
typedef enum {
    FN_LIGHT1 = 0, FN_LIGHT2, FN_HUMIDIFIER, FN_DEHUMID, FN_WATERING,
    FN_FAN, FN_INLINEFAN, FN_HEATING, FN_DEVICE1, FN_DEVICE2, FN_MAX
} func_id_t;

// Auto-Schalten einer Funktion (von der Klimaregelung). Löst Funktion→Steckdose
// auf und ruft relay_set_auto; bei „nicht belegt" (-1) passiert nichts.
esp_err_t relay_fn_set_auto(func_id_t fn, bool on);

int8_t   relay_fn_get(func_id_t fn);            // zugeordnete Steckdose (-1 = keine)
void     relay_fn_set(func_id_t fn, int8_t phys); // Zuordnung setzen + speichern
const char *func_name(func_id_t fn);

// ── 2-Kammer-Zuordnung ──
// Jede Steckdose bekommt eine Kammer (0=A, 1=B, -1=keine/manuell) + eine Rolle
// (func_id_t). Dimmer und Abluft-iFan ebenfalls einer Kammer. NVS-persistiert.
#define N_CHAMBERS 2

int8_t    relay_chamber(relay_id_t id);            // 0/1/-1
func_id_t relay_role(relay_id_t id);
void      relay_assign(relay_id_t id, int8_t chamber, func_id_t role);
// physische Steckdose mit (chamber, role) finden → Index oder -1
int       relay_find(int chamber, func_id_t role);
// erste Steckdose mit dieser Rolle (Kammer egal) → Index oder -1
int       relay_role_find(func_id_t role);

int8_t dim_chamber(int ch);                        // Dimmer-Kammer (0/1/-1)
void   dim_set_chamber(int ch, int8_t chamber);
int8_t ifan_chamber(void);                         // iFan-Kammer
void   ifan_set_chamber(int8_t chamber);
