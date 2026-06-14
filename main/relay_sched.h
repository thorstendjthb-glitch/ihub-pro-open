// relay_sched.h — Zeitpläne pro Steckdose (Tagesplan oder Zyklus).
// Priorität: MANUELL > Zeitplan > Klimaregelung/Bewässerungs-Automatik.
// Eine Steckdose mit aktivem Zeitplan (mode != 0) wird für relay_set_auto
// gesperrt (Claim in relays.c) — die Regelung lässt sie dann in Ruhe.
#pragma once
#include <stdint.h>
#include <time.h>

typedef struct {
    uint8_t  mode;        // 0=aus, 1=Tagesplan, 2=Zyklus
    uint16_t on_min;      // Tagesplan: Einschalt-Minute des Tages (0..1439)
    uint16_t off_min;     // Tagesplan: Ausschalt-Minute (über Mitternacht erlaubt)
    uint16_t cyc_on_min;  // Zyklus: Minuten AN
    uint16_t cyc_off_min; // Zyklus: Minuten AUS (Raster ab Mitternacht gerechnet)
} relay_sched_t;

void relay_sched_init(void);              // Pläne aus NVS laden + Claims setzen
relay_sched_t *relay_sched(int relay);    // Plan einer Steckdose (editierbar)
void relay_sched_save(void);              // alle Pläne in NVS + Claims neu anwenden

// Im Regel-Task aufrufen (2-s-Zyklus). Tut nichts bei ungültiger Systemzeit.
void relay_sched_tick(const struct tm *now);
