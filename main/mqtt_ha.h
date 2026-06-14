// mqtt_ha.h — MQTT-Client mit Home-Assistant-Auto-Discovery
//
// Published Discovery-Configs (switch/light/fan/sensor) unter
// <prefix>/<component>/<device>/<object>/config und abonniert die
// Command-Topics. State-Updates via mqtt_ha_publish_*.
#pragma once
#include "esp_err.h"
#include "devices.h"
#include "bl0940.h"
#include "climate_control.h"

// Verbindet zum Broker, published Discovery + abonniert Commands.
esp_err_t mqtt_ha_start(void);

// Sensorwerte an HA publishen (State-Topics).
void mqtt_ha_publish_sensors(const sensor_data_t *d);

// Energiemesswerte (BL0940) an HA publishen.
void mqtt_ha_publish_power(const power_data_t *p);

// Klima-Bedarf (ac_demand/ac_mode) publishen — für HA-Automation der Medion-Klima.
void mqtt_ha_publish_climate(const climate_status_t *c);

// Relais-/Light-/Aktor-Zustände publishen (z. B. nach Command oder Boot).
void mqtt_ha_publish_relay(int relay_id, bool on);
void mqtt_ha_publish_light(int ch, uint8_t pct);

// Alle Relais-/Dimmer-Zustände gegen den zuletzt publizierten Stand diffen und
// Änderungen publishen (retained). Zyklisch aus dem Publish-Task aufrufen — fängt
// ALLE lokalen Schaltquellen ein (Regelung, WebUI, Zeitpläne, Bewässerung, Tasten);
// vorher sah HA nur Boot-Zustand + eigene MQTT-Befehle (Bug: Switches blieben "off").
void mqtt_ha_publish_outputs(void);
