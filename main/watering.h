// watering.h — automatische Bewässerung (Intervall oder Substratfeuchte-gesteuert).
// Schaltet die Steckdose mit Rolle „Bewässerung" (relay_role_find) über relay_set_auto
// → manueller Override (MANUAL) und Steckdosen-Zeitplan haben Vorrang. Die globale
// Not-Abschaltung (water_max_min, check_watering_timeout) bleibt unabhängig davon aktiv.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "devices.h"

typedef struct {
    uint8_t  mode;          // 0=aus (nur manuell), 1=Intervall, 2=Substratfeuchte
    uint8_t  only_day;      // 1 = nur während der Lichtphase gießen
    uint16_t duration_s;    // Gieß-Dauer pro Vorgang in Sekunden
    uint16_t interval_h;    // Modus Intervall: alle X Stunden
    uint8_t  moist_low;     // Modus Substratfeuchte: Start unter X %
    uint16_t min_pause_min; // Modus Substratfeuchte: Mindestpause zwischen Vorgängen (min)
} watering_cfg_t;

void watering_init(void);                 // Config + letzter Gieß-Zeitpunkt aus NVS
watering_cfg_t *watering_cfg(void);       // editierbar (danach watering_save_cfg)
void watering_save_cfg(void);

// Im Regel-Task aufrufen (2-s-Zyklus): startet/beendet Gießvorgänge.
void watering_tick(const sensor_data_t *d, time_t now);

// Status fürs Dashboard/JSON: aktiv?, letzter Start (Epoch, 0=nie),
// nächster fälliger Start (nur Intervall-Modus, sonst 0).
void watering_status(bool *active, uint32_t *last_start, uint32_t *next_due);
