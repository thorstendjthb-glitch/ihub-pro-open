// modbus_master.h — Minimaler Modbus-RTU-Master für die beiden RS-485-Busse
//
// Beide Busse: 115200 8N1, Halbduplex, /RE fest auf GND, DE per GPIO.
// Sensor-Bus (UART1): TH3in1, CO2, PAR, Substrat, iFan, Humidifier
// Fan-Bus   (UART2): Clip-Fan (Broadcast 0x10)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"

typedef struct {
    uart_port_t uart;
    gpio_num_t  tx;
    gpio_num_t  rx;
    gpio_num_t  de;     // Driver Enable (HIGH = senden)
    uint32_t    baud;
} mb_bus_t;

// Initialisiert UART + DE-Pin eines Busses.
esp_err_t mb_bus_init(const mb_bus_t *bus);

// Function 0x03 — Read Holding Registers.
// liest `count` 16-bit-Register ab `start_reg` von `slave` nach out[] (Big-Endian).
// Gibt ESP_OK bei gültiger Antwort + korrekter CRC.
esp_err_t mb_read_holding(const mb_bus_t *bus, uint8_t slave,
                          uint16_t start_reg, uint16_t count, uint16_t *out);

// Function 0x04 — Read Input Registers.
esp_err_t mb_read_input(const mb_bus_t *bus, uint8_t slave,
                        uint16_t start_reg, uint16_t count, uint16_t *out);

// Function 0x06 — Write Single Register.
esp_err_t mb_write_single(const mb_bus_t *bus, uint8_t slave,
                          uint16_t reg, uint16_t value);

// Function 0x10 — Write Multiple Registers (z. B. Clip-Fan-Broadcast an Slave 0).
esp_err_t mb_write_multiple(const mb_bus_t *bus, uint8_t slave,
                            uint16_t start_reg, const uint16_t *values, uint16_t count);

// Modbus-CRC16 (Polynom 0xA001). Öffentlich für Tests/vorgefertigte Frames.
uint16_t mb_crc16(const uint8_t *data, size_t len);

// DEBUG: Roh-Frame mit temporärer Baudrate senden (CRC16 wird angehängt).
// Für Clip-Fan-RE (eigenes Protokoll @ 9600 Baud). Rückgabe = empfangene Bytes.
int mb_send_raw(const mb_bus_t *bus, uint32_t baud, const uint8_t *payload,
                size_t len, uint8_t *resp, size_t resp_max);
