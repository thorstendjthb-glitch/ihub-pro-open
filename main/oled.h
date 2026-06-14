// oled.h — SSD1306 128x64 I2C-Statusdisplay (OPTIONAL).
//
// Aktivierung: board_pins.h → OLED_ENABLED auf 1 setzen (NACHDEM die
// SMD-Widerstände an GPIO1/2 entfernt wurden) und neu flashen.
// Solange OLED_ENABLED == 0 sind alle Funktionen No-Ops und GPIO1/2
// werden NICHT konfiguriert.
#pragma once
#include "esp_err.h"
#include <stdbool.h>

// Initialisiert I2C-Bus + SSD1306. Bei OLED_ENABLED==0 sofortiger
// No-Op-Return (ESP_OK), ohne GPIO1/2 oder I2C anzufassen.
esp_err_t oled_init(void);

// Startet den Render-Task (zeigt Klima/Status beider Kammern + Uhr).
// No-Op wenn deaktiviert oder Init fehlschlug.
void oled_start(void);

// true, wenn das Display erfolgreich initialisiert wurde und aktiv ist.
bool oled_is_active(void);
