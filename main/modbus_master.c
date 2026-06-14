// modbus_master.c — Minimaler Modbus-RTU-Master
#include "modbus_master.h"
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <string.h>

static const char *TAG = "modbus";

#define MB_RX_TIMEOUT_MS  200   // Max-Wartezeit auf einen Slave; vorhandene kehren sofort nach
                                // dem Frame zurück (expected-Länge), nur Abwesende warten voll
#define MB_MAX_FRAME      260

uint16_t mb_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc; // Low-Byte zuerst senden
}

esp_err_t mb_bus_init(const mb_bus_t *bus)
{
    uart_config_t uc = {
        .baud_rate = bus->baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(bus->uart, 512, 512, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(bus->uart, &uc));
    // DE-Pin als RTS legen → Hardware steuert Sende-/Empfangsumschaltung bit-genau.
    ESP_ERROR_CHECK(uart_set_pin(bus->uart, bus->tx, bus->rx,
                                 bus->de, UART_PIN_NO_CHANGE));
    // ESP-IDF Hardware-RS-485-Halbduplex: RTS=DE wird automatisch getoggelt.
    ESP_ERROR_CHECK(uart_set_mode(bus->uart, UART_MODE_RS485_HALF_DUPLEX));

    ESP_LOGI(TAG, "Bus UART%d init RS485-HW (TX=%d RX=%d DE/RTS=%d @ %lu)",
             bus->uart, bus->tx, bus->rx, bus->de, (unsigned long)bus->baud);
    return ESP_OK;
}

// Sendet ein Frame (CRC wird angehängt) und liest die Antwort.
// `expected` = erwartete Antwortlänge in Bytes: uart_read_bytes kehrt zurück, sobald so viele
// Bytes da sind ODER der Timeout greift. Mit resp_max (260) wartete früher JEDE Transaktion den
// vollen Timeout, weil 260 Bytes nie kamen — auch bei vorhandenen Slaves. expected<=0 = keine
// Antwort erwartet (Broadcast) → nur kurz drainen, nicht blockieren.
static int mb_txn(const mb_bus_t *bus, uint8_t *req, size_t req_len,
                  uint8_t *resp, size_t resp_max, int expected)
{
    uint16_t crc = mb_crc16(req, req_len);
    req[req_len++] = crc & 0xFF;
    req[req_len++] = crc >> 8;

    uart_flush_input(bus->uart);

    // Im RS485-HW-Modus schaltet die UART den DE/RTS-Pin selbst.
    uart_write_bytes(bus->uart, req, req_len);
    uart_wait_tx_done(bus->uart, pdMS_TO_TICKS(100));

    if (expected <= 0) {                       // Broadcast: keine Antwort erwartet, nur drainen
        uint8_t dr[16];
        while (uart_read_bytes(bus->uart, dr, sizeof(dr), pdMS_TO_TICKS(4)) > 0) { }
        return 0;
    }
    int want = (expected < (int)resp_max) ? expected : (int)resp_max;
    int n = uart_read_bytes(bus->uart, resp, want, pdMS_TO_TICKS(MB_RX_TIMEOUT_MS));
    // WICHTIG (Halbduplex): nachlaufende Bytes leerdrainen, bis die Leitung ~4 ms still ist.
    // Sendet ein Slave mehr Bytes als angefragt, würden sie sonst mit der NÄCHSTEN Anfrage
    // kollidieren und den darauffolgenden Sensor verstummen lassen (Regression PAR/iFan).
    uint8_t dr[16];
    while (uart_read_bytes(bus->uart, dr, sizeof(dr), pdMS_TO_TICKS(4)) > 0) { }
    return n;
}

esp_err_t mb_read_holding(const mb_bus_t *bus, uint8_t slave,
                          uint16_t start_reg, uint16_t count, uint16_t *out)
{
    uint8_t req[8] = {
        slave, 0x03,
        start_reg >> 8, start_reg & 0xFF,
        count >> 8, count & 0xFF,
    };
    uint8_t resp[MB_MAX_FRAME];
    int expected = 5 + count * 2; // addr+func+bytecount + data + crc
    int n = mb_txn(bus, req, 6, resp, sizeof(resp), expected);
    if (n < expected) {
        ESP_LOGW(TAG, "slave 0x%02X: kurze/keine Antwort (%d/%d)", slave, n, expected);
        return ESP_ERR_TIMEOUT;
    }
    if (resp[0] != slave || resp[1] != 0x03 || resp[2] != count * 2) {
        // Einzel-Glitch (RS-485-Rauschen) — mb_rd() retryt; nur DEBUG, kein Log-Spam.
        // Bleibt es dauerhaft, schlagen alle Retries fehl → sensor_valid=false → Alarm.
        ESP_LOGD(TAG, "slave 0x%02X: Frame-Header ungültig (Retry)", slave);
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint16_t crc = mb_crc16(resp, 3 + count * 2);
    if ((crc & 0xFF) != resp[3 + count * 2] || (crc >> 8) != resp[4 + count * 2]) {
        ESP_LOGD(TAG, "slave 0x%02X: CRC-Fehler (Retry)", slave);
        return ESP_ERR_INVALID_CRC;
    }
    for (int i = 0; i < count; i++) {
        out[i] = (resp[3 + i * 2] << 8) | resp[4 + i * 2];
    }
    return ESP_OK;
}

// Function 0x04 — Read Input Registers (manche Sensoren legen Messwerte hierhin).
esp_err_t mb_read_input(const mb_bus_t *bus, uint8_t slave,
                        uint16_t start_reg, uint16_t count, uint16_t *out)
{
    uint8_t req[8] = {
        slave, 0x04,
        start_reg >> 8, start_reg & 0xFF,
        count >> 8, count & 0xFF,
    };
    uint8_t resp[MB_MAX_FRAME];
    int expected = 5 + count * 2;
    int n = mb_txn(bus, req, 6, resp, sizeof(resp), expected);
    if (n < expected) return ESP_ERR_TIMEOUT;
    if (resp[0] != slave || resp[1] != 0x04 || resp[2] != count * 2)
        return ESP_ERR_INVALID_RESPONSE;
    uint16_t crc = mb_crc16(resp, 3 + count * 2);
    if ((crc & 0xFF) != resp[3 + count * 2] || (crc >> 8) != resp[4 + count * 2])
        return ESP_ERR_INVALID_CRC;
    for (int i = 0; i < count; i++)
        out[i] = (resp[3 + i * 2] << 8) | resp[4 + i * 2];
    return ESP_OK;
}

esp_err_t mb_write_single(const mb_bus_t *bus, uint8_t slave,
                          uint16_t reg, uint16_t value)
{
    uint8_t req[8] = {
        slave, 0x06,
        reg >> 8, reg & 0xFF,
        value >> 8, value & 0xFF,
    };
    uint8_t resp[MB_MAX_FRAME];
    int n = mb_txn(bus, req, 6, resp, sizeof(resp), slave == 0 ? 0 : 8);
    // Echo-Antwort (8 Byte) erwartet; Slave 0 (Broadcast) antwortet nicht.
    if (slave == 0) return ESP_OK;
    if (n < 8) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

// DEBUG: Roh-Frame mit temporärer Baudrate senden. `payload` ohne CRC (wird
// angehängt). Schaltet UART auf `baud`, sendet, liest Antwort, schaltet zurück.
// Rückgabe: Anzahl empfangener Bytes (Antwort in resp[]).
int mb_send_raw(const mb_bus_t *bus, uint32_t baud, const uint8_t *payload,
                size_t len, uint8_t *resp, size_t resp_max)
{
    uint8_t req[MB_MAX_FRAME];
    if (len == 0 || len > MB_MAX_FRAME - 2) return -1;
    memcpy(req, payload, len);
    uint16_t crc = mb_crc16(req, len);
    req[len++] = crc & 0xFF;
    req[len++] = crc >> 8;

    if (baud && baud != bus->baud) uart_set_baudrate(bus->uart, baud);
    uart_flush_input(bus->uart);
    uart_write_bytes(bus->uart, req, len);
    uart_wait_tx_done(bus->uart, pdMS_TO_TICKS(50));
    // Fire-and-Forget: der Clip-Fan antwortet nicht. Nur kurz drainen (kein 300-ms-Stall,
    // sonst blockiert der Keepalive den Fan-Bus und der Dachboden-Poll flackert).
    int n = 0;
    if (resp && resp_max) n = uart_read_bytes(bus->uart, resp, resp_max, pdMS_TO_TICKS(15));
    if (baud && baud != bus->baud) uart_set_baudrate(bus->uart, bus->baud);
    return n;
}

esp_err_t mb_write_multiple(const mb_bus_t *bus, uint8_t slave,
                            uint16_t start_reg, const uint16_t *values, uint16_t count)
{
    uint8_t req[MB_MAX_FRAME];
    int p = 0;
    req[p++] = slave;
    req[p++] = 0x10;
    req[p++] = start_reg >> 8;  req[p++] = start_reg & 0xFF;
    req[p++] = count >> 8;      req[p++] = count & 0xFF;
    req[p++] = count * 2;       // byte count
    for (int i = 0; i < count; i++) {
        req[p++] = values[i] >> 8;
        req[p++] = values[i] & 0xFF;
    }
    uint8_t resp[MB_MAX_FRAME];
    int n = mb_txn(bus, req, p, resp, sizeof(resp), slave == 0 ? 0 : 8);
    if (slave == 0) return ESP_OK; // Broadcast: keine Antwort
    if (n < 8) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}
