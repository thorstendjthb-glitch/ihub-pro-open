// buttons.h — Front-Taster mit Haltezeit-Logik
//   KEY_AC (Power): 5 s halten  → Notaus (alle Aktoren aus)
//   KEY_BT:         2 s halten  → Grow-Profil weiterschalten
//                  10 s halten  → Werksreset (NVS löschen, Neustart; AP-Setup folgt)
// Buzzer gibt Feedback beim Erreichen jeder Schwelle.
#pragma once
#include "esp_err.h"

esp_err_t buttons_init(void);
