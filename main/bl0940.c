// bl0940.c — BL0940 Energy-Meter-Treiber
#include "bl0940.h"
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "bl0940";

// Während einer Diagnose pausiert der normale Poll (sonst UART-Konflikt).
static volatile bool s_diag_busy = false;

#define BL_ADDR_READ   0x58
#define BL_FULL_READ   0xAA   // liest komplettes 35-Byte-Paket
#define BL_FRAME_LEN   35
#define BL_HEADER      0x55

// ── Umrechnungsfaktoren ──
// v_rms/i_rms gegen 232,8 V Netz kalibriert (2026-06-01/02).
// WATT NACHKALIBRIERT 2026-06-05: alte PREF=489,87 (gegen einen Fön als „2500-W"-Last) zeigte
// ~25 % zu hoch. Gegen die zwei Grow-Lampen (Aufdruck 465 W + 150 W @ 100 %) per BL0940-
// Leistungsmessung verifiziert → Korrekturfaktor 0,8 → PREF = 489,87 / 0,8 = 612,34.
// (Restfehler ±5 %: die zwei Lampen brauchen leicht verschiedene Faktoren, BL0940 nicht 100 % linear.)
#define BL_UREF  14354.0f      // v_rms / UREF = Volt (nachkalibriert 2026-06-02: war 13771 → 10V zu hoch)
#define BL_IREF  227663.0f     // i_rms / IREF = Ampere
#define BL_PREF  612.34f       // watt  / PREF = Watt
// Energie: 1 cf_cnt-Impuls = 1/EREF kWh
#define BL_EREF  (3600000.0f * BL_PREF / (1638.4f * 256.0f))

static inline uint32_t u24le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static inline int32_t i24le(const uint8_t *p) {
    int32_t v = u24le(p);
    if (v & 0x800000) v -= 0x1000000;   // Vorzeichen-Erweiterung
    return v;
}

esp_err_t bl0940_init(void)
{
    uart_config_t uc = {
        .baud_rate  = BL0940_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BL0940_UART_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BL0940_UART_NUM, &uc));
    ESP_ERROR_CHECK(uart_set_pin(BL0940_UART_NUM, BL0940_TX_PIN, BL0940_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // (BL0940-Optokoppler-Signalrichtung: separat zu klären — Invertierung der
    //  RX-Leitung flutet UART0 im Idle, daher hier nicht aktiv.)
    ESP_LOGI(TAG, "BL0940 init (UART%d TX=%d RX=%d @ %d, via Optokoppler)",
             BL0940_UART_NUM, BL0940_TX_PIN, BL0940_RX_PIN, BL0940_BAUD);
    return ESP_OK;
}

int bl0940_diag(int tx_inv, int rx_inv, int baud, int listen_only, int swap_pins, char *out, int outlen)
{
    s_diag_busy = true;
    vTaskDelay(pdMS_TO_TICKS(20));   // laufenden Poll-Read auslaufen lassen

    // Optional TX/RX-Pins tauschen (falls GPIO43/44 in der Rolle vertauscht sind)
    if (swap_pins)
        uart_set_pin(BL0940_UART_NUM, BL0940_RX_PIN, BL0940_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uint32_t mask = 0;
    if (tx_inv) mask |= UART_SIGNAL_TXD_INV;
    if (rx_inv) mask |= UART_SIGNAL_RXD_INV;
    uart_set_baudrate(BL0940_UART_NUM, baud);
    uart_set_line_inverse(BL0940_UART_NUM, mask ? mask : UART_SIGNAL_INV_DISABLE);
    uart_flush_input(BL0940_UART_NUM);

    if (!listen_only) {
        uint8_t cmd[2] = { BL_ADDR_READ, BL_FULL_READ };
        uart_write_bytes(BL0940_UART_NUM, cmd, sizeof(cmd));
    }

    uint8_t buf[64];
    int n = uart_read_bytes(BL0940_UART_NUM, buf, sizeof(buf),
                            pdMS_TO_TICKS(listen_only ? 1200 : 400));

    int p = 0;
    if (outlen > 0) out[0] = 0;
    for (int i = 0; i < n && p < outlen - 4; i++)
        p += snprintf(out + p, outlen - p, "%02X ", buf[i]);

    // Standard wiederherstellen
    uart_set_baudrate(BL0940_UART_NUM, BL0940_BAUD);
    uart_set_line_inverse(BL0940_UART_NUM, UART_SIGNAL_INV_DISABLE);
    if (swap_pins)   // Pins zurück auf Standardbelegung
        uart_set_pin(BL0940_UART_NUM, BL0940_TX_PIN, BL0940_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_flush_input(BL0940_UART_NUM);
    s_diag_busy = false;
    return n;
}

esp_err_t bl0940_read(power_data_t *out)
{
    out->valid = false;
    if (s_diag_busy) return ESP_FAIL;   // Diagnose läuft → Poll aussetzen

    uint8_t cmd[2] = { BL_ADDR_READ, BL_FULL_READ };
    uart_flush_input(BL0940_UART_NUM);
    uart_write_bytes(BL0940_UART_NUM, cmd, sizeof(cmd));

    uint8_t pkt[BL_FRAME_LEN];
    int n = uart_read_bytes(BL0940_UART_NUM, pkt, BL_FRAME_LEN, pdMS_TO_TICKS(200));
    if (n < BL_FRAME_LEN) {
        ESP_LOGW(TAG, "kurze Antwort (%d/%d)", n, BL_FRAME_LEN);
        return ESP_ERR_TIMEOUT;
    }
    if (pkt[0] != BL_HEADER) {
        ESP_LOGW(TAG, "falscher Header 0x%02X", pkt[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    // Checksum: ~(0x58 + alle Paket-Bytes inkl. Header, ohne Checksum) & 0xFF
    // (am realen Paket verifiziert: Header MUSS mitsummiert werden → i ab 0).
    uint8_t sum = BL_ADDR_READ;
    for (int i = 0; i < BL_FRAME_LEN - 1; i++) sum += pkt[i];
    sum = ~sum;
    if (sum != pkt[BL_FRAME_LEN - 1]) {
        ESP_LOGW(TAG, "Checksum-Fehler (calc 0x%02X != 0x%02X)", sum, pkt[BL_FRAME_LEN - 1]);
        return ESP_ERR_INVALID_CRC;
    }

    // Feld-Offsets (siehe bl0940.h Kommentar)
    uint32_t i_rms  = u24le(&pkt[4]);
    uint32_t v_rms  = u24le(&pkt[10]);
    int32_t  watt   = i24le(&pkt[16]);
    uint32_t cf_cnt = u24le(&pkt[22]);
    uint16_t tps1   = pkt[25] | (pkt[26] << 8);

    out->voltage_v     = v_rms / BL_UREF;
    out->current_a     = i_rms / BL_IREF;
    out->power_w       = watt  / BL_PREF;
    out->energy_kwh    = cf_cnt / BL_EREF;
    // Chip-Temp (Datenblatt-Näherung): (tps1/2 - 64) °C grob
    out->temperature_c = (tps1 / 2.0f) - 64.0f;
    // Plausi-Filter: Negativwerte (Rauschen/Vorzeichen) auf 0; grobe Ausreißer verwerfen,
    // damit keine Spitzen in History/HA/Anzeige gelangen (230-V-Anlage).
    if (out->power_w   < 0) out->power_w   = 0;
    if (out->current_a < 0) out->current_a = 0;
    if (out->voltage_v < 0) out->voltage_v = 0;
    if (out->voltage_v > 300.0f || out->current_a > 30.0f || out->power_w > 6000.0f) {
        out->valid = false;
        return ESP_ERR_INVALID_RESPONSE;   // unplausibler Frame → verwerfen
    }
    out->valid = true;
    return ESP_OK;
}
