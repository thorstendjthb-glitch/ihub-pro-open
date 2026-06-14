// webui_diag.c — WebUI-Teilmodul: Diagnose-/Debug-Endpunkte
// (Modbus-Scan/-Write, BL0940-Diagnose, BLE-Liste, GPIO-/LED-Test sowie die
//  Reverse-Engineering-Helfer für Clip-Fan-Rohframes). Nicht fürs Dashboard nötig,
//  aber wertvoll für Inbetriebnahme + Protokoll-Analyse am lebenden Gerät.
#include "webui_internal.h"
#include "devices.h"
#include "bl0940.h"
#include "ble_sensors.h"
#include "status_leds.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <stdio.h>
#include <stdlib.h>

// DEBUG: rohe Clip-Fan-Register (Schwenk-Reverse-Engineering) —
// /api/clipraw?reg=10&a=&b=&c=&d=   (reg dezimal, Default 10 = 0x0A)
static esp_err_t clipraw_post(httpd_req_t *req)
{
    int reg = webui_qint(req, "reg", 10);
    int bus = webui_qint(req, "bus", 1);
    int a = webui_qint(req, "a", 0), b = webui_qint(req, "b", 0), c = webui_qint(req, "c", 0), d = webui_qint(req, "d", 0);
    clipfan_raw4(bus, reg, a, b, c, d);
    return httpd_resp_sendstr(req, "ok");
}

// DEBUG: OEM-Aktor-Broadcast auf Reg 1001 nachspielen —
// /api/fanbc?r9=<shakeLevel>&r10=<reg1011>&r11=<reg1012>   (Defaults 5/0/0)
static esp_err_t fanbc_post(httpd_req_t *req)
{
    int bus = webui_qint(req, "bus", 0);   // Default Sensor-Bus (OEM-Sammelbus)
    int pos = webui_qint(req, "pos", -1);
    if (pos >= 0) {                  // Sweep-Modus: nur eine Position im 26er-Block setzen
        fan_bcast1001_pos(bus, pos, webui_qint(req, "val", 100));
        return httpd_resp_sendstr(req, "ok");
    }
    int r9  = webui_qint(req, "r9", 5);
    int r10 = webui_qint(req, "r10", 0);
    int r11 = webui_qint(req, "r11", 0);
    fan_bcast1001(bus, r9, r10, r11);
    return httpd_resp_sendstr(req, "ok");
}

// DEBUG: Clip-Fan eigenes Protokoll @ 9600 (aus FW-Dump): Frame 02 01 01 07 01 <enc> +CRC16.
// /api/fanframe?bus=1&baud=9600&enc=53   oder  ?hex=02-01-01-07-01-35  (CRC wird angehängt)
static esp_err_t fanframe_post(httpd_req_t *req)
{
    int bus  = webui_qint(req, "bus", 1);
    int baud = webui_qint(req, "baud", 9600);
    int enc  = webui_qint(req, "enc", 0);
    uint8_t payload[16];
    int len = 0;
    char q[160], v[80];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "hex", v, sizeof(v)) == ESP_OK) {
        char *p = v;
        while (*p && len < 16) {
            payload[len++] = (uint8_t)strtol(p, &p, 16);
            while (*p && *p != '0' && (*p < '1' || *p > '9') &&
                   (*p < 'a' || *p > 'f') && (*p < 'A' || *p > 'F')) p++;
        }
    }
    if (len == 0) {   // Standard-Fan-Frame aus dem Dump
        payload[0]=0x02; payload[1]=0x01; payload[2]=0x01;
        payload[3]=0x07; payload[4]=0x01; payload[5]=(uint8_t)enc; len=6;
    }
    uint8_t resp[64];
    int n = fan_raw(bus, baud, payload, len, resp, sizeof(resp));
    char out[160]; int o = snprintf(out, sizeof(out), "sent %d B @ %d, resp=%d:", len, baud, n);
    for (int i = 0; i < n && i < 20 && o < 150; i++) o += snprintf(out+o, sizeof(out)-o, " %02X", resp[i]);
    return httpd_resp_sendstr(req, out);
}

// DEBUG: freien GPIO als Ausgang testen (Inline-Fan-Pin-Suche).
//  /api/gpiotest?pin=X&lvl=0|1            → digital
//  /api/gpiotest?pin=X&pwm=0..100         → 25 kHz PWM (Kanal 4, Timer 1)
static esp_err_t gpiotest_post(httpd_req_t *req)
{
    int pin = webui_qint(req, "pin", -1);
    if (pin < 0 || pin > 48) return httpd_resp_sendstr(req, "err: pin");
    int pwm = webui_qint(req, "pwm", -1);
    if (pwm >= 0) {
        if (pwm > 100) pwm = 100;
        ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                  .timer_num = LEDC_TIMER_1, .freq_hz = 25000, .clk_cfg = LEDC_AUTO_CLK };
        ledc_timer_config(&t);
        ledc_channel_config_t c = { .gpio_num = pin, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_4,
                                    .timer_sel = LEDC_TIMER_1, .duty = (255 * pwm) / 100, .hpoint = 0 };
        ledc_channel_config(&c);
        char o[48]; snprintf(o, sizeof(o), "gpio %d PWM %d%%", pin, pwm);
        return httpd_resp_sendstr(req, o);
    }
    int lvl = webui_qint(req, "lvl", 0);
    gpio_config_t io = { .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT,
                         .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE };
    gpio_config(&io);
    gpio_set_level((gpio_num_t)pin, lvl ? 1 : 0);
    char o[48]; snprintf(o, sizeof(o), "gpio %d = %d", pin, lvl ? 1 : 0);
    return httpd_resp_sendstr(req, o);
}

// Diagnose: LEDs direkt testen — /api/ledtest?m=0..4
static esp_err_t ledtest_post(httpd_req_t *req)
{
    status_leds_test(webui_qint(req, "m", 0));
    return httpd_resp_sendstr(req, "ok");
}

// Diagnose: gefundene BLE-Geräte (für TP357-Dekodierung) — /api/ble
static esp_err_t ble_get(httpd_req_t *req)
{
    char *buf = malloc(5120);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    ble_sensors_list(buf, 5120);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return e;
}

// Diagnose: Holding-Register schreiben — /api/mbwrite?slave=&reg=&val=
static esp_err_t mbwrite_post(httpd_req_t *req)
{
    int sl = webui_qint(req, "slave", -1), reg = webui_qint(req, "reg", -1), val = webui_qint(req, "val", -1);
    if (sl < 1 || reg < 0 || val < 0) return httpd_resp_sendstr(req, "err: slave/reg/val noetig");
    esp_err_t e = devices_mb_write((uint8_t)sl, (uint16_t)reg, (uint16_t)val);
    return httpd_resp_sendstr(req, e == ESP_OK ? "ok" : "fail");
}

// Diagnose: Modbus-Bus scannen — /api/mbscan?from=1&to=32
static esp_err_t mbscan_get(httpd_req_t *req)
{
    int bus = webui_qint(req, "bus", 0);    // 0 = Sensor-Bus, 1 = Fan-Bus
    int from = webui_qint(req, "from", 1), to = webui_qint(req, "to", 32);
    int count = webui_qint(req, "count", 13);
    int func = webui_qint(req, "func", 3);
    if (from < 1) from = 1;
    if (to > 247) to = 247;
    char map[768];
    int n = devices_mb_scan((uint8_t)bus, (uint8_t)from, (uint8_t)to, (uint16_t)count, (uint8_t)func, map, sizeof(map));
    char resp[900];
    int m = snprintf(resp, sizeof(resp),
        "{\"found\":%d,\"bus\":%d,\"from\":%d,\"to\":%d,\"count\":%d,\"func\":%d,\"map\":\"%s\"}", n, bus, from, to, count, func, map);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, m);
}

// Diagnose BL0940 — /api/bl0940diag?tx=0|1&rx=0|1&baud=4800&listen=0|1
// Probiert Polarität/Baud durch und gibt empfangene Rohbytes als Hex zurück.
static esp_err_t bldiag_get(httpd_req_t *req)
{
    int tx = webui_qint(req, "tx", 0), rx = webui_qint(req, "rx", 0);
    int baud = webui_qint(req, "baud", 4800), listen = webui_qint(req, "listen", 0);
    int swap = webui_qint(req, "swap", 0);
    char hex[200];
    int n = bl0940_diag(tx, rx, baud, listen, swap, hex, sizeof(hex));
    char resp[320];
    int m = snprintf(resp, sizeof(resp),
        "{\"n\":%d,\"tx_inv\":%d,\"rx_inv\":%d,\"baud\":%d,\"listen\":%d,\"swap\":%d,\"hex\":\"%s\"}",
        n, tx, rx, baud, listen, swap, hex);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, m);
}

// Diagnose-Werkzeuge komplett über webui_guard (auch die GETs — Bus-Scans/GPIO-Tests
// sind Eingriffe am lebenden Gerät, kein Dashboard-Bedarf).
#define GUARDED(u, m, h) { .uri = (u), .method = (m), .handler = webui_guard, .user_ctx = (void *)(h) }
static const httpd_uri_t k_uris[] = {
    GUARDED("/api/clipraw",    HTTP_POST, clipraw_post),
    GUARDED("/api/fanbc",      HTTP_POST, fanbc_post),
    GUARDED("/api/fanframe",   HTTP_POST, fanframe_post),
    GUARDED("/api/gpiotest",   HTTP_POST, gpiotest_post),
    GUARDED("/api/ledtest",    HTTP_POST, ledtest_post),
    GUARDED("/api/bl0940diag", HTTP_GET,  bldiag_get),
    GUARDED("/api/mbscan",     HTTP_GET,  mbscan_get),
    GUARDED("/api/mbwrite",    HTTP_POST, mbwrite_post),
    GUARDED("/api/ble",        HTTP_GET,  ble_get),
};
#undef GUARDED

esp_err_t webui_diag_register(httpd_handle_t srv)
{
    for (size_t i = 0; i < sizeof(k_uris) / sizeof(k_uris[0]); i++)
        httpd_register_uri_handler(srv, &k_uris[i]);
    return ESP_OK;
}
