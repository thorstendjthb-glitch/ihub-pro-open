// buzzer.h — Buzzer (LEDC-Ton) für Alarme/Feedback
#pragma once
#include "esp_err.h"

esp_err_t buzzer_init(void);
void buzzer_beep(int freq_hz, int ms);   // einzelner Ton (blockierend)
void buzzer_alarm(void);                 // mehrfacher Warnton (Schimmel/Temp)
void buzzer_tone_ms(int freq_hz, int ms); // Dauerton für ms, NICHT blockierend (esp_timer-Auto-Stop)
