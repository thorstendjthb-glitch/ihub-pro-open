// ble_sensors.h — passiver BLE-Scanner für Funk-Klimasensoren (NimBLE Observer)
#pragma once
#include <stdbool.h>
#include <stdint.h>

void ble_sensors_start(void);

// Diagnose: gefundene BLE-Geräte als JSON-Array nach out schreiben.
// Format: [{"mac":"..","name":"..","rssi":-x,"raw":"HEX..."}, ...]. Rückgabe = Anzahl.
int ble_sensors_list(char *out, int outlen);

// Temp/Feuchte eines erkannten BLE-Klimasensors per MAC (addr.val-Reihenfolge, LSB zuerst).
// Rückgabe true, wenn gefunden + gültig.
bool ble_get_th(const uint8_t mac[6], float *temp, float *hum);
