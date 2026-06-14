// status_leds.h — Front-Status-LED (EINE zweifarbige 2-Pin-LED zwischen GPIO4/GPIO5,
// antiparallel — siehe board_pins.h). Differentiell angesteuert, immer nur eine Farbe:
//   verbunden → Farbe A dauerhaft · WLAN-Suche → Farbe A blinkt
//   Alarm (Priorität) → Farbe B blinkt
#pragma once
#include "esp_err.h"

esp_err_t status_leds_start(void);

// Diagnose: LEDs direkt ansteuern (umgeht WLAN-/Alarm-Logik).
// 0=normal, 1=nur GPIO4 an, 2=nur GPIO5 an, 3=nur GPIO4 blinkt, 4=nur GPIO5 blinkt.
void status_leds_test(int mode);
