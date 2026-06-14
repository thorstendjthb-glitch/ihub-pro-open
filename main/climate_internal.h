// climate_internal.h — modulinterne Schnittstelle zwischen den Klima-Teilmodulen:
//   climate_config.c   Sollwerte/Phasen/Zuordnung/Overrides + NVS-Persistenz
//   climate_control.c  Regelung, Alarme, Regel-Task (einziger Modbus-Bus-Nutzer)
// NICHT von anderen Modulen einbinden — öffentliche API ist climate_control.h.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Lädt alle persistierten Konfigurations-Teile aus NVS (Phasen, Sollwerte,
// globale Config, Grow-Zyklus, manuelle Overrides). Von climate_init() gerufen.
esp_err_t climate_config_init(void);

// Clip-Fan-Direktbefehl abholen: true genau einmal nach climate_set_clipfan()
// (bzw. nach Boot mit gespeicherten Werten). Der Regel-Task wendet ihn dann auf
// dem RS-485-Bus an — so gibt es keinen Bus-Konflikt mit dem Sensor-Poll.
bool climate_clipfan_consume_dirty(void);
