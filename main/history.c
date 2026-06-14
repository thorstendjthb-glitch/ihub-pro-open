// history.c — Sensor-Verlauf: feiner 7-Tage-Ring (5 min, DRAM) + grober Langzeit-Ring
// (1 h, ~120 Tage, PSRAM). Beide flash-persistent in der „spiffs"-Datenpartition
// (Rohspeicher, kein Dateisystem), je 2 alternierende Slots mit Magic+Seq+CRC32.
#include "history.h"
#include "state.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <limits.h>
#include <stdbool.h>

static const char *TAG = "history";

#define NV       INT16_MIN          // "kein Wert"
#define HP_MAGIC 0x48495332u        // "HIS2" (16 Kanäle inkl. Wasserwerte)

// ── Ring-Beschreibung (Kurzzeit + Langzeit teilen sich die gesamte Logik) ──
typedef struct {
    hist_sample_t *buf;      // Puffer (DRAM oder PSRAM); NULL = Ring nicht verfügbar
    int       n;             // Kapazität
    int       count, head;   // Füllstand + Schreibposition
    time_t    last;          // Epoch des neuesten Samples
    int       interval_s;    // Sample-Abstand
    uint32_t  seq;           // Flash-Sequenznummer
    uint32_t  flash_base;    // Byte-Offset des Slot-Paars in der Partition
    uint32_t  slot_sz;       // Slot-Größe (>= Header + Puffer, 4-KB-gerundet)
} hist_ring_t;

static hist_sample_t s_sbuf[HIST_N];   // Kurzzeit: statisch im internen DRAM
static hist_ring_t s_short = { .buf = s_sbuf, .n = HIST_N, .interval_s = HIST_INTERVAL_S,
                               .flash_base = 0x00000, .slot_sz = 0x10000 };  // 64-KB-Slots 0..0x20000
static hist_ring_t s_long  = { .buf = NULL,   .n = HIST_LONG_N, .interval_s = HIST_LONG_INT_S,
                               .flash_base = 0x20000, .slot_sz = 0x18000 };  // 96-KB-Slots 0x20000..0x50000

static const esp_partition_t *s_part;

static bool part_ok(void)
{
    if (!s_part)
        s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                          ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    return s_part != NULL;
}

static int16_t sc(bool ok, float v, float mul)
{
    if (!ok) return NV;
    float x = v * mul;
    if (x > 32760) x = 32760; if (x < -32760) x = -32760;
    return (int16_t)x;
}

// Einen Messwert-Snapshot aus dem aktuellen Zustand bauen (für beide Ringe identisch).
static void build_sample(hist_sample_t *s, const sensor_data_t *d,
                         const power_data_t *pw, const climate_status_t *st)
{
    chamber_state_t ca, cb;
    climate_chamber(0, &ca);
    climate_chamber(1, &cb);
    s->a_temp = sc(ca.valid, ca.temp, 10.0f);
    s->a_rh   = sc(ca.valid, ca.rh,   10.0f);
    s->a_vpd  = sc(ca.valid, ca.vpd, 100.0f);
    s->b_temp = sc(cb.valid, cb.temp, 10.0f);
    s->b_rh   = sc(cb.valid, cb.rh,   10.0f);
    s->b_vpd  = sc(cb.valid, cb.vpd, 100.0f);
    s->co2    = d->co2_valid ? (int16_t)d->co2_ppm : NV;
    s->power  = pw->valid ? (int16_t)pw->power_w : NV;
    s->ifan   = (int16_t)st->ifan_pct;

    water_data_t w; state_get_water(&w);
    bool wf = water_is_fresh(&w, HIST_INTERVAL_S + 60);
    s->w_ph   = sc(wf && w.ph_valid,       w.ph,           100.0f);
    s->w_orp  = sc(wf && w.orp_valid,      w.orp_mv,         1.0f);
    s->w_temp = sc(wf && w.temp_valid,     w.temp_c,        10.0f);
    s->w_ec   = sc(wf && w.ec_valid,       w.ec_us,          1.0f);
    s->w_tds  = sc(wf && w.tds_valid,      w.tds_ppm,        1.0f);
    s->w_sal  = sc(wf && w.salinity_valid, w.salinity_ppm,   1.0f);
    s->w_sg   = sc(wf && w.sg_valid,       w.sg,          1000.0f);
}

static void ring_push(hist_ring_t *r, const hist_sample_t *s, time_t now)
{
    if (!r->buf) return;
    r->last = now;
    if (r->count < r->n) r->buf[r->count++] = *s;
    else { r->buf[r->head] = *s; r->head = (r->head + 1) % r->n; }
}

void history_tick(time_t now, const sensor_data_t *d, const power_data_t *pw, const climate_status_t *st)
{
    bool need_short = (s_short.last == 0 || (now - s_short.last) >= s_short.interval_s);
    bool need_long  = (s_long.buf && (s_long.last == 0 || (now - s_long.last) >= s_long.interval_s));
    if (!need_short && !need_long) return;
    hist_sample_t s; build_sample(&s, d, pw, st);
    if (need_short) ring_push(&s_short, &s, now);
    if (need_long)  ring_push(&s_long,  &s, now);
}

static int16_t pick(const hist_sample_t *s, int metric)
{
    switch (metric) {
        case 0:  return s->a_temp;  case 1:  return s->a_rh;   case 2:  return s->a_vpd;
        case 3:  return s->b_temp;  case 4:  return s->b_rh;   case 5:  return s->b_vpd;
        case 6:  return s->co2;     case 7:  return s->power;  case 8:  return s->ifan;
        case 9:  return s->w_ph;    case 10: return s->w_orp;  case 11: return s->w_temp;
        case 12: return s->w_ec;    case 13: return s->w_tds;  case 14: return s->w_sal;
        default: return s->w_sg;    // 15
    }
}

int history_get(int metric, int16_t *out, int maxn, int *interval, uint32_t *newest, bool longterm)
{
    hist_ring_t *r = longterm ? &s_long : &s_short;
    if (interval) *interval = r->interval_s;
    if (newest)   *newest   = (uint32_t)r->last;
    if (!r->buf) return 0;                                  // Langzeit ohne PSRAM → leer
    int n = r->count < maxn ? r->count : maxn;
    for (int i = 0; i < n; i++)
        out[i] = pick(&r->buf[(r->head + i) % r->n], metric);
    return n;
}

// ── Flash-Persistenz (generisch für beide Ringe) ────────────────────────────
typedef struct {
    uint32_t magic, seq;
    int32_t  count, head;
    uint32_t last;               // Epoch des neuesten Samples
    float    dli;                // DLI-Tagessumme (nur Kurzzeit-Ring nutzt das)
    int32_t  yday;               // Jahrestag der DLI-Summe
    uint32_t crc;                // CRC32 über den Puffer
} hist_hdr_t;

static void ring_save(hist_ring_t *r, float dli, int yday)
{
    if (!part_ok() || !r->buf || r->count == 0) return;
    size_t bufbytes = (size_t)r->n * sizeof(hist_sample_t);
    size_t erase = (sizeof(hist_hdr_t) + bufbytes + 4095) & ~(size_t)4095;
    if (erase > r->slot_sz) { ESP_LOGE(TAG, "Slot zu klein (%u > %u)", (unsigned)erase, (unsigned)r->slot_sz); return; }
    int slot = (int)((r->seq + 1) & 1);
    uint32_t off = r->flash_base + (uint32_t)slot * r->slot_sz;
    hist_hdr_t h = {
        .magic = HP_MAGIC, .seq = r->seq + 1,
        .count = r->count, .head = r->head, .last = (uint32_t)r->last,
        .dli = dli, .yday = yday,
        .crc = esp_rom_crc32_le(0, (const uint8_t *)r->buf, bufbytes),
    };
    if (esp_partition_erase_range(s_part, off, erase) != ESP_OK ||
        esp_partition_write(s_part, off, &h, sizeof(h)) != ESP_OK ||
        esp_partition_write(s_part, off + sizeof(h), r->buf, bufbytes) != ESP_OK) {
        ESP_LOGW(TAG, "Sicherung fehlgeschlagen (base 0x%x, Slot %d)", (unsigned)r->flash_base, slot);
        return;
    }
    r->seq++;
}

static bool ring_try_load(hist_ring_t *r, int slot, const hist_hdr_t *h, float *dli, int *yday)
{
    size_t bufbytes = (size_t)r->n * sizeof(hist_sample_t);
    uint32_t off = r->flash_base + (uint32_t)slot * r->slot_sz + sizeof(hist_hdr_t);
    if (esp_partition_read(s_part, off, r->buf, bufbytes) != ESP_OK) return false;
    if (esp_rom_crc32_le(0, (const uint8_t *)r->buf, bufbytes) != h->crc) return false;
    r->count = h->count; r->head = h->head; r->last = (time_t)h->last; r->seq = h->seq;
    if (dli)  *dli  = h->dli;
    if (yday) *yday = h->yday;
    return true;
}

static bool ring_restore(hist_ring_t *r, float *dli, int *yday)
{
    if (!part_ok() || !r->buf) return false;
    hist_hdr_t h[2]; bool v[2];
    for (int i = 0; i < 2; i++) {
        v[i] = (esp_partition_read(s_part, r->flash_base + (uint32_t)i * r->slot_sz, &h[i], sizeof(h[i])) == ESP_OK)
               && h[i].magic == HP_MAGIC
               && h[i].count >= 0 && h[i].count <= r->n
               && h[i].head  >= 0 && h[i].head  <  r->n;
    }
    int first = -1, second = -1;
    if (v[0] && v[1]) { first = (h[1].seq > h[0].seq) ? 1 : 0; second = 1 - first; }
    else if (v[0]) first = 0;
    else if (v[1]) first = 1;
    if (first  >= 0 && ring_try_load(r, first,  &h[first],  dli, yday)) return true;
    if (second >= 0 && ring_try_load(r, second, &h[second], dli, yday)) return true;
    r->count = 0; r->head = 0;
    return false;
}

bool history_restore(float *dli_today, int *yday)
{
    // Langzeit-Puffer einmalig im PSRAM anlegen (fällt sonst lautlos weg → nur 7-Tage-Verlauf).
    if (!s_long.buf) {
        s_long.buf = heap_caps_malloc((size_t)HIST_LONG_N * sizeof(hist_sample_t), MALLOC_CAP_SPIRAM);
        if (!s_long.buf)
            ESP_LOGW(TAG, "PSRAM für Langzeit-Ring nicht verfügbar → nur 7-Tage-Verlauf");
    }
    if (!part_ok()) {
        ESP_LOGW(TAG, "keine Datenpartition → Verlauf nur im RAM");
        return false;
    }
    bool short_ok = ring_restore(&s_short, dli_today, yday);
    if (ring_restore(&s_long, NULL, NULL))
        ESP_LOGI(TAG, "Langzeit-Verlauf wiederhergestellt (%d Samples)", s_long.count);
    ESP_LOGI(TAG, "Kurzzeit-Verlauf %s (%d Samples)", short_ok ? "wiederhergestellt" : "leer", s_short.count);
    return short_ok;   // Rückgabe = Kurzzeit (trägt DLI für climate_init)
}

void history_persist_tick(time_t now, uint16_t save_min, float dli_today, int yday)
{
    static int64_t s_last_save_us = 0;
    (void)now;
    if (save_min == 0 || s_short.count == 0) return;
    int64_t t = esp_timer_get_time();                      // monoton, SNTP-Sprünge egal
    if (!s_last_save_us) { s_last_save_us = t; return; }   // nach Boot erst Intervall abwarten
    if ((t - s_last_save_us) < (int64_t)save_min * 60000000LL) return;
    s_last_save_us = t;
    ring_save(&s_short, dli_today, yday);
    ring_save(&s_long, 0, 0);                              // Langzeit-Ring mitsichern (kein DLI)
}
