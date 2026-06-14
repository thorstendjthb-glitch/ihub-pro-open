// ble_sensors.c — passiver BLE-Scanner (NimBLE Observer) für Funk-Klimasensoren.
// Hört nur Advertisements mit (kein Pairing/Connect) und hält eine Tabelle der
// zuletzt gesehenen Geräte inkl. Roh-Advertisement (zum Dekodieren z.B. TP357).
#include "ble_sensors.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble";

#define MAXDEV 28
typedef struct {
    bool     used;
    uint8_t  mac[6];
    char     name[24];
    int8_t   rssi;
    uint8_t  raw[40];
    uint8_t  raw_len;
    bool     th;        // erkannter Temp/Feuchte-Sensor (TP357)
    float    temp;      // °C
    float    hum;       // %
} dev_t;

static dev_t s_dev[MAXDEV];
static SemaphoreHandle_t s_mtx;
static uint8_t s_own_addr_type;

static void store_adv(const struct ble_gap_disc_desc *d)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int slot = -1, freeslot = -1, weakest = -1;
    bool matched = false;
    for (int i = 0; i < MAXDEV; i++) {
        if (s_dev[i].used && memcmp(s_dev[i].mac, d->addr.val, 6) == 0) { slot = i; matched = true; break; }
        if (!s_dev[i].used && freeslot < 0) freeslot = i;
        if (s_dev[i].used && (weakest < 0 || s_dev[i].rssi < s_dev[weakest].rssi)) weakest = i;
    }
    if (!matched) {
        if (freeslot >= 0) slot = freeslot;
        else if (weakest >= 0 && d->rssi > s_dev[weakest].rssi) slot = weakest;  // volles Tabelle: schwächstes verdrängen
        if (slot < 0) { xSemaphoreGive(s_mtx); return; }
        memset(&s_dev[slot], 0, sizeof(s_dev[slot]));   // frischer Eintrag
    }

    dev_t *e = &s_dev[slot];
    e->used = true;
    memcpy(e->mac, d->addr.val, 6);
    e->rssi = d->rssi;
    // Roh-Adv (mit Temp/Feuchte) NUR aus echten Advertisements übernehmen, nicht
    // aus Scan-Responses (die tragen meist nur den Namen).
    if (d->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
        int n = d->length_data; if (n > (int)sizeof(e->raw)) n = sizeof(e->raw);
        memcpy(e->raw, d->data, n);
        e->raw_len = (uint8_t)n;
    }

    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0) {
        if (f.name && f.name_len) {
            int nl = f.name_len; if (nl > 23) nl = 23;
            memcpy(e->name, f.name, nl); e->name[nl] = 0;
        }
        // TP357: Hersteller-Daten, Temp = Bytes[1..2] LE ÷10, Feuchte = Byte[3]
        if (f.mfg_data && f.mfg_data_len >= 4) {
            int16_t t = (int16_t)(f.mfg_data[1] | (f.mfg_data[2] << 8));
            e->temp = t / 10.0f;
            e->hum  = f.mfg_data[3];
        }
    }
    e->th = (strncmp(e->name, "TP35", 4) == 0);   // als Klimasensor erkannt
    xSemaphoreGive(s_mtx);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_DISC) store_adv(&event->disc);
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params dp = {0};
    dp.passive = 0;            // aktiv → Scan-Responses (Gerätenamen) werden geholt
    dp.filter_duplicates = 0;
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &dp, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    start_scan();
    ESP_LOGI(TAG, "BLE-Scan läuft (passiv)");
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_sensors_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
}

bool ble_get_th(const uint8_t mac[6], float *temp, float *hum)
{
    bool ok = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < MAXDEV; i++) {
        if (s_dev[i].used && s_dev[i].th && memcmp(s_dev[i].mac, mac, 6) == 0) {
            if (temp) *temp = s_dev[i].temp;
            if (hum)  *hum  = s_dev[i].hum;
            ok = true; break;
        }
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

int ble_sensors_list(char *out, int outlen)
{
    int p = 0, cnt = 0;
    if (outlen) out[0] = 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    p += snprintf(out + p, outlen - p, "[");
    for (int i = 0; i < MAXDEV; i++) {
        if (!s_dev[i].used) continue;
        dev_t *e = &s_dev[i];
        p += snprintf(out + p, outlen - p,
            "%s{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"name\":\"%s\",\"rssi\":%d,\"raw\":\"",
            cnt ? "," : "", e->mac[5], e->mac[4], e->mac[3], e->mac[2], e->mac[1], e->mac[0],
            e->name, e->rssi);
        for (int j = 0; j < e->raw_len && p < outlen - 10; j++)
            p += snprintf(out + p, outlen - p, "%02X", e->raw[j]);
        p += snprintf(out + p, outlen - p, "\",\"th\":%s,\"temp\":%.1f,\"hum\":%.0f}",
                      e->th ? "true" : "false", e->temp, e->hum);
        cnt++;
        if (p > outlen - 140) break;
    }
    p += snprintf(out + p, outlen - p, "]");
    xSemaphoreGive(s_mtx);
    return cnt;
}
