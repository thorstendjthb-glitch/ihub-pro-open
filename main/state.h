// state.h — Zentraler, Mutex-geschützter Zustands-Cache.
// EIN Task (climate) pollt den Modbus-Bus; MQTT + WebUI lesen nur aus dem Cache.
// Verhindert konkurrierenden Bus-Zugriff aus mehreren Tasks.
#pragma once
#include "devices.h"
#include "bl0940.h"
#include "climate_control.h"
#include "water.h"

esp_err_t state_init(void);

// Schreiber (nur climate_task)
void state_set_sensors(const sensor_data_t *d);
void state_set_power(const power_data_t *p);
void state_set_climate(const climate_status_t *c);
// Wasserwerte — Schreiber ist der MQTT-Task (Empfang von HA), nicht climate.
void state_set_water(const water_data_t *w);

// Leser (MQTT, WebUI) — kopieren unter Mutex
void state_get_sensors(sensor_data_t *out);
void state_get_power(power_data_t *out);
void state_get_climate(climate_status_t *out);
void state_get_water(water_data_t *out);
