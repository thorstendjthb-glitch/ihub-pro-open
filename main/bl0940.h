// bl0940.h — Treiber für den BL0940 Energy-Meter (UART0, via 2 Optokoppler)
//
// Protokoll: 4800 Baud, 8N1 (Datenblatt: 1.5 Stop-Bits, RX toleriert 1),
// Half-Duplex. Master sendet 0x58 0xAA (Full-Read), Slave antwortet mit
// 35-Byte-Paket (Header 0x55 + Felder + Checksum).
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool  valid;
    float voltage_v;     // Netzspannung
    float current_a;     // Strom
    float power_w;       // Wirkleistung
    float energy_kwh;    // kumulierte Energie (aus cf_cnt)
    float temperature_c; // interne Chip-Temperatur (tps1)
} power_data_t;

esp_err_t bl0940_init(void);

// Liest ein Full-Read-Paket, validiert Checksum, füllt `out`.
esp_err_t bl0940_read(power_data_t *out);

// Diagnose: UART0 temporär mit gegebener Polarität/Baud konfigurieren, optional
// den Read-Befehl senden, dann lauschen. Schreibt die empfangenen Rohbytes als
// Hex-String nach `out`. Rückgabe = Anzahl empfangener Bytes. Stellt danach die
// Standardkonfiguration wieder her. Pausiert den normalen Poll während des Tests.
int bl0940_diag(int tx_inv, int rx_inv, int baud, int listen_only, int swap_pins, char *out, int outlen);
