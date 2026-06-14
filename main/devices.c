// devices.c — High-Level-Zugriff auf die RJ12-Modbus-Geräte
#include "devices.h"
#include "modbus_master.h"
#include "climate_control.h"   // climate_global() für CO2-Baseline (FRC)
#include "board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "devices";

// Während eines Bus-Scans pausiert der reguläre Poll (sonst RS-485-Konflikt).
static volatile bool s_scan_busy = false;

// Serialisiert ALLE Fan-Bus-Zugriffe (Clip-Fan-Keepalive ↔ Dachboden-TH3in1-Poll),
// da der Clip-Fan die Baudrate auf 9600 umschaltet und der Sensor 115200 nutzt.
static SemaphoreHandle_t s_fan_mtx;

static void clipfan_task(void *arg);   // Keepalive-Task (Definition weiter unten)

static const mb_bus_t s_sensor_bus = {
    .uart = SENSOR_UART_NUM, .tx = SENSOR_TX_PIN, .rx = SENSOR_RX_PIN,
    .de = SENSOR_DE_PIN, .baud = 115200,
};
static const mb_bus_t s_fan_bus = {
    .uart = FAN_UART_NUM, .tx = FAN_TX_PIN, .rx = FAN_RX_PIN,
    .de = FAN_DE_PIN, .baud = 115200,
};

// Lese-Helfer mit Retry NUR bei Frame-/CRC-Fehlern (Motor-EMV des Lüfters).
// Bei echtem Timeout (Gerät nicht angeschlossen) sofort abbrechen → Poll bleibt schnell.
static bool mb_rd(const mb_bus_t *bus, uint8_t slave, uint16_t count, uint16_t *reg)
{
    for (int i = 0; i < 3; i++) {
        esp_err_t r = mb_read_holding(bus, slave, 0, count, reg);
        if (r == ESP_OK) return true;
        if (r == ESP_ERR_TIMEOUT) return false;   // keine Antwort → nicht da, nicht retrien
        vTaskDelay(pdMS_TO_TICKS(8));              // Störung → kurz warten, erneut
    }
    return false;
}

esp_err_t devices_init(void)
{
    ESP_ERROR_CHECK(mb_bus_init(&s_sensor_bus));
    ESP_ERROR_CHECK(mb_bus_init(&s_fan_bus));
    s_fan_mtx = xSemaphoreCreateMutex();
    // Keepalive-Task für den Clip-/Schwenk-Lüfter (eigenes 9600/115200-Protokoll).
    xTaskCreate(clipfan_task, "clipfan", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "Modbus-Busse bereit (Sensor + Fan) + Clip-Fan-Task");
    return ESP_OK;
}

// Diagnose: einen Modbus-Bus nach antwortenden Slaves absuchen.
// `bus` = 0 Sensor-Bus, 1 Fan-Bus. `count` = Anzahl Register (ab 0) je Slave (1..32).
int devices_mb_scan(uint8_t bus, uint8_t from, uint8_t to, uint16_t count, uint8_t func, char *out, int outlen)
{
    const mb_bus_t *b = (bus == 1) ? &s_fan_bus : &s_sensor_bus;
    if (count < 1)  count = 13;
    if (count > 32) count = 32;
    s_scan_busy = true;
    vTaskDelay(pdMS_TO_TICKS(40));   // laufenden Poll auslaufen lassen
    int found = 0, p = 0;
    if (outlen) out[0] = 0;
    uint16_t reg[32];
    for (uint8_t a = from; a >= 1 && a <= to; a++) {
        esp_err_t r = ESP_FAIL;
        for (int t = 0; t < 3; t++) {     // wie der Poll: bei Frame-/CRC-Fehler erneut
            r = (func == 4) ? mb_read_input(b, a, 0, count, reg)
                            : mb_read_holding(b, a, 0, count, reg);
            if (r == ESP_OK || r == ESP_ERR_TIMEOUT) break;   // OK oder „nicht da" → fertig
            vTaskDelay(pdMS_TO_TICKS(8));
        }
        if (r == ESP_OK) {
            found++;
            p += snprintf(out + p, outlen - p, "0x%02X:", a);
            for (int i = 0; i < count && p < outlen - 12; i++)
                p += snprintf(out + p, outlen - p, " %u", reg[i]);
            p += snprintf(out + p, outlen - p, " | ");
            if (p > outlen - 60) break;
        }
        if (a == 0xFF) break;   // Überlauf-Schutz
    }
    s_scan_busy = false;
    return found;
}

// Diagnose: einzelnes Holding-Register auf dem Sensor-Bus schreiben (Func 0x06).
esp_err_t devices_mb_write(uint8_t slave, uint16_t reg, uint16_t val)
{
    s_scan_busy = true;
    vTaskDelay(pdMS_TO_TICKS(40));
    esp_err_t r = mb_write_single(&s_sensor_bus, slave, reg, val);
    s_scan_busy = false;
    return r;
}

esp_err_t devices_poll(sensor_data_t *o)
{
    uint16_t reg[24];
    memset(o, 0, sizeof(*o));   // erst alles 0 — nicht-antwortende Sensoren liefern keinen Müll
    if (s_scan_busy) return ESP_OK;   // Bus-Scan läuft → diesen Zyklus aussetzen

    // TH3in1: 22 Register, Temp=R10, rH=R11, Licht=R12.
    // Werksadresse 0x02, aber manche Exemplare wurden auf 0x0F umadressiert (flüchtig,
    // überlebt keinen Stromausfall). Daher beide Adressen probieren und die zuletzt
    // erfolgreiche merken → kein dauerhafter Doppel-Timeout im Poll.
    static uint8_t s_th_addr = SLAVE_TH3IN1;
    o->th3in1_valid = mb_rd(&s_sensor_bus, s_th_addr, 13, reg);
    if (!o->th3in1_valid) {
        uint8_t alt = (s_th_addr == SLAVE_TH3IN1) ? SLAVE_TH3IN1_ALT : SLAVE_TH3IN1;
        o->th3in1_valid = mb_rd(&s_sensor_bus, alt, 13, reg);
        if (o->th3in1_valid) s_th_addr = alt;   // gefunden → künftig direkt diese Adresse
    }
    if (o->th3in1_valid) {
        o->temperature_c = (int16_t)reg[10] / 10.0f;
        o->humidity_pct  = reg[11] / 10.0f;
        o->light_lux     = reg[12];
        // Plausi-Filter: ein defekter Sensor (z.B. 0xFFFF) darf keine Müllwerte in die
        // Regelung speisen. Unplausibel → wie Sensor-Ausfall behandeln (Failsafe greift).
        if (o->temperature_c < -40.0f || o->temperature_c > 85.0f ||
            o->humidity_pct  < 0.0f   || o->humidity_pct  > 100.0f)
            o->th3in1_valid = false;
    }

    // 2. TH3in1 (Dachboden/Quellluft) auf dem FAN-Bus, Werksadresse 0x02 — eigener
    // RS-485-Bus, daher KEIN Adresskonflikt mit dem Kammer-Sensor und kein Umadressieren
    // nötig (die Sensor-Adressänderung ist flüchtig → überlebt keinen Stromausfall).
    if (s_fan_mtx) xSemaphoreTake(s_fan_mtx, portMAX_DELAY);
    o->attic_valid = mb_rd(&s_fan_bus, SLAVE_TH3IN1, 13, reg);
    if (o->attic_valid) {
        o->attic_temp_c       = (int16_t)reg[10] / 10.0f;
        o->attic_humidity_pct = reg[11] / 10.0f;
        if (o->attic_temp_c < -40.0f || o->attic_temp_c > 85.0f ||
            o->attic_humidity_pct < 0.0f || o->attic_humidity_pct > 100.0f)
            o->attic_valid = false;   // unplausibel → Referenz verwerfen (Free-Cooling-Schutz)
    }
    if (s_fan_mtx) xSemaphoreGive(s_fan_mtx);

    // CO2 = Reg10 (Mess-Anteil) + konfigurierbare Frischluft-Baseline (FRC, Default 420 ppm).
    // DISASSEMBLY-BEWIESEN (s. docs/MODBUS_REGISTERS.md): die Original-FW nimmt Reg10 OHNE
    // Baseline → unphysikalisch ~0. Wir kalibrieren stattdessen auf den atmosphärischen
    // Frischluftwert. Reg10/Reg12 bleiben als Debug-Rohwerte (co2_raw/co2_base) exportiert.
    o->co2_valid = mb_rd(&s_sensor_bus, SLAVE_CO2, 13, reg);
    if (o->co2_valid) {
        uint16_t base = climate_global()->co2_baseline;
        if (base == 0) base = 420;          // Sentinel: 0/altes NVS-Padding → Default 420
        o->co2_raw  = (int)reg[10];
        o->co2_base = (int)reg[12];          // Reg12 (roh, nur Debug — fließt NICHT in die Rechnung)
        o->co2_ppm  = o->co2_raw + (int)base;
    }

    // PAR: PPFD=R11
    o->par_valid = mb_rd(&s_sensor_bus, SLAVE_PAR, 13, reg);
    if (o->par_valid) o->ppfd = reg[11];

    // Substrat (Slave 0x01): Daten um +1 Register verschoben ggü. der 0x14-Map
    // (Serien-Nr. 6059 + Konstant-Register 5/4): Temp=R11, Feuchte=R12, EC=R13.
    // FW-Dump-Verifikation 2026-06-03: OEM-Parser (MH-STHE, Typ 0xBA02) liest exakt
    // diese 3 Register als Temp=int16/10 (signed!), Feuchte=uint16/10, EC=uint16/100.
    o->substrate_valid = mb_rd(&s_sensor_bus, SLAVE_SUBSTRATE, 14, reg);
    if (o->substrate_valid) {
        o->soil_moist_pct = reg[12] / 10.0f;     // bestätigt (86% im Wasser)
        o->soil_ec        = reg[13] / 100.0f;    // bestätigt (0.3 realistisch)
        // Substrat-Temp: laut OEM-Dump reg11 = signed int16 / 10 °C. Die früher als
        // "widersprüchlich" notierten Rohwerte 270/397 sind 27.0/39.7 °C — beide gültig
        // (Fühler-Eigenwärme an der Luft), KEIN Defekt. Mit Plausi-Filter aktiviert.
        float st = (int16_t)reg[11] / 10.0f;
        o->soil_temp_valid = (st > -20.0f && st < 80.0f);
        o->soil_temp_c     = o->soil_temp_valid ? st : 0.0f;
    }

    // iFan/iFresh (0x06): wird OPEN-LOOP gesteuert (Master-Heartbeat unten + Schreibbefehle
    // 06 06 00 0F/10 in ifan_set). Auf diesen Status-Read antwortet er nicht zuverlässig, und
    // das Feedback (ifan_speed_pct) wird NIRGENDS verwendet → kein Poll. Spart 200 ms/Zyklus
    // und vermeidet die Dauer-Log-Warnung "slave 0x06: keine Antwort". Steuerung unberührt.
    o->ifan_valid = false;

    // ── Master-Heartbeat (Reg 1001 Broadcast) ──
    // Der iFresh/MH-AE (Slave 0x06) entscheidet BEIM EINSCHALTEN Master(autark) vs. Slave
    // anhand dieses OEM-Aktor-Broadcasts. Ohne ihn kommt er autark hoch und ignoriert
    // alle Befehle. Mit ihm wird er Slave → antwortet auf Polls + gehorcht `06 06 00 10`.
    // Inhalt 1:1 wie OEM-Mitschnitt 2026-06-04 (reg[8]=5, reg[10]=0x40, Rest 0).
    {
        uint16_t hb[26] = {0};
        hb[8]  = 0x0005;
        hb[10] = 0x0040;
        mb_write_multiple(&s_sensor_bus, 0x00, 0x03E9, hb, 26);
    }

    return ESP_OK;
}

esp_err_t humidifier_set(int level)
{
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    // Master-Sequenz: erst Power (Reg12), dann Stufe (Reg14)
    mb_write_single(&s_sensor_bus, SLAVE_HUMIDIFIER, 0x000C, level > 0 ? 1 : 0);
    return mb_write_single(&s_sensor_bus, SLAVE_HUMIDIFIER, 0x000E, level);
}

esp_err_t ifan_set(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    // iFresh/MH-AE (Slave 0x06) braucht ZWEI Register (wie der Humidifier):
    //   Reg 0x0F = Steuermodus (1 = iHub-gesteuert), Reg 0x10 = Drehzahl in %.
    // WICHTIG: Reg 0x0F = 0 heißt NICHT „aus", sondern „Steuerung loslassen" → der Lüfter
    // geht auf Failsafe-VOLLGAS (am Gerät bestätigt: 0%/onoff=0 → 77,8 W). Deshalb halten
    // wir Reg 0x0F IMMER auf 1; Drehzahl 0 = langsamste Stufe, echtes Aus über das Relais.
    // Voraussetzung: Master-Heartbeat (Reg 1001) läuft → iFresh ist im Slave-Modus.
    mb_write_single(&s_sensor_bus, SLAVE_IFAN, 0x000F, 1);
    return mb_write_single(&s_sensor_bus, SLAVE_IFAN, 0x0010, pct);
}

// ── Clip-/Schwenk-Lüfter — eigenes Protokoll auf dem Fan-Bus ──
// Reverse-engineert aus Original-FW-Dump (app_fan_bus, fan_set @ 0x4201289c) +
// Bus-Replay 2026-06-04. Der Lüfter hört auf ADRESSE 0x02 und braucht ZWEI Frames,
// die der OEM abwechselnd im Dauertakt (~200 ms) sendet:
//   1) Schwenk + Natural-Wind @ 9600 Baud:   02 01 01 07 01 <enc> +CRC16
//      enc = Bitfeld aus shake/natural (siehe clipfan_enc()).
//   2) Drehzahl @ 115200 Baud (Modbus 0x10): 02 10 00 0A 00 04 08 [stufe,schwenk,0,natural]
// Wichtig: Der Lüfter hält den Zustand nur bei periodischer Nachspeisung → Keepalive-Task.
// (Früher nie gefunden, weil unser Fan-Bus fix auf 115200 stand und wir nur Broadcast 0x00 sendeten.)
static int  s_clip_stufe   = 0;     // 0-10
static int  s_clip_schwenk = 0;     // 0=aus, 5=45°, 10=90°
static bool s_clip_natural = false;

// enc-Byte für den 9600-Frame (Bitfeld aus dem Dump, fkt 0x42012830).
// WICHTIG: unteres Nibble = DREHZAHL (stufe), Bits 5/6 = Schwenkwinkel (schwenk) —
// zwei getrennte Felder! (Bestätigt am Gerät 2026-06-04: schwenk steuerte fälschlich die Drehzahl.)
static uint8_t clipfan_enc(int stufe, int schwenk, bool natural)
{
    uint8_t e = (uint8_t)(stufe & 0x0F);            // Drehzahl 0-10
    if (stufe > 0)                     e |= 0x10;   // Lüfter an
    if (schwenk >= 1 && schwenk <= 5)  e |= 0x20;   // Schwenk ≈45°
    if (schwenk >= 6)                  e |= 0x40;   // Schwenk ≈90°
    if (natural)                       e |= 0x80;   // Natural-Wind
    return e;
}

// Sendet EINEN OEM-Zyklus (beide Frames) mit effektiver Drehzahl `eff`.
// Baud wird je Frame umgeschaltet.
static void clipfan_send_cycle(int eff)
{
    uint8_t resp[16];
    if (s_scan_busy) return;       // Bus-Scan läuft → Fan-Bus nicht anfassen
    if (s_fan_mtx) xSemaphoreTake(s_fan_mtx, portMAX_DELAY);   // Fan-Bus exklusiv (Baud-Wechsel!)
    // Natural-Bit (Bit7) bewusst NICHT setzen: der Lüfter würde sonst die Drehzahl-Nibble
    // ignorieren und nur konstant absenken. Die "Brise" erzeugen wir per Software-Modulation
    // der Drehzahl (eff) im Keepalive-Task.
    // 1) Drehzahl(=eff)+Schwenk @ 9600
    uint8_t f1[6] = { 0x02, 0x01, 0x01, 0x07, 0x01,
                      clipfan_enc(eff, s_clip_schwenk, false) };
    mb_send_raw(&s_fan_bus, 9600, f1, sizeof(f1), resp, sizeof(resp));
    // 2) Drehzahl @ 115200 — Reg 0x0A, 4 Register [eff, schwenk, 0, 0]
    uint8_t f2[15] = {
        0x02, 0x10, 0x00, 0x0A, 0x00, 0x04, 0x08,
        0x00, (uint8_t)eff,
        0x00, (uint8_t)s_clip_schwenk,
        0x00, 0x00,
        0x00, 0x00,
    };
    mb_send_raw(&s_fan_bus, 115200, f2, sizeof(f2), resp, sizeof(resp));
    if (s_fan_mtx) xSemaphoreGive(s_fan_mtx);
}

// Keepalive: speist den Zustand periodisch nach (OEM-Verhalten). Bei Natural-Wind
// wird die Drehzahl in Software langsam moduliert (Dreieck zwischen ~40% und 100%),
// denn der Lüfter selbst kann das nicht — im OEM macht das ebenfalls die Software
// (bsp_fan_task, Periode ~stufe×5 Zyklen).
static void clipfan_task(void *arg)
{
    uint32_t phase = 0;
    for (;;) {
        if (s_clip_stufe > 0 || s_clip_schwenk > 0 || s_clip_natural) {
            int eff = s_clip_stufe;
            if (s_clip_natural && s_clip_stufe > 0) {
                int lo = s_clip_stufe / 3;            // ~33 %
                if (lo < 1) lo = 1;
                int span = s_clip_stufe - lo;
                const int half = 12;                  // halbe Periode: 12×0.4 s ≈ 5 s (volle ~10 s)
                int p = (int)(phase % (uint32_t)(2 * half));
                int tri = (p < half) ? p : (2 * half - p);   // 0..half..0
                eff = lo + (span * tri) / half;
                phase++;
            } else {
                phase = 0;
            }
            clipfan_send_cycle(eff);
        }
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

esp_err_t clipfan_set(int stufe, int schwenk, bool natural_wind)
{
    if (stufe < 0) stufe = 0;     if (stufe > 10) stufe = 10;
    if (schwenk < 0) schwenk = 0; if (schwenk > 10) schwenk = 10;
    s_clip_stufe   = stufe;
    s_clip_schwenk = schwenk;
    s_clip_natural = natural_wind;
    clipfan_send_cycle(s_clip_stufe);   // sofort wirksam (nicht auf den Task warten)
    return ESP_OK;
}

// DEBUG: 4 rohe Clip-Fan-Register ab `startreg` per Broadcast schreiben (Schwenk-RE).
// bus: 0 = Sensor-Bus, 1 = Fan-Bus.
esp_err_t clipfan_raw4(int bus, int startreg, int a, int b, int c, int d)
{
    const mb_bus_t *bp = (bus == 0) ? &s_sensor_bus : &s_fan_bus;
    uint16_t vals[4] = { (uint16_t)a, (uint16_t)b, (uint16_t)c, (uint16_t)d };
    return mb_write_multiple(bp, 0x00, (uint16_t)startreg, vals, 4);
}

// DEBUG: OEM-Aktor-Broadcast auf Reg 1001 (0x03E9), 26 Register, nachspielen.
// Aus den Mitschnitten: alles 0 außer reg[8]=shakeLevel, reg[10]=Bitfeld(reg1011),
// reg[11]=flag(reg1012). Genau dieser Frame hat im OEM den Lüfter gesteuert.
// bus: 0 = Sensor-Bus (OEM-Sammelbus!), 1 = Fan-Bus.
esp_err_t fan_bcast1001(int bus, int r1009, int r1011, int r1012)
{
    const mb_bus_t *bp = (bus == 0) ? &s_sensor_bus : &s_fan_bus;
    uint16_t vals[26] = {0};
    vals[8]  = (uint16_t)r1009;   // shakeLevel (Schwenk)
    vals[10] = (uint16_t)r1011;   // Bitfeld Drehzahl/Power
    vals[11] = (uint16_t)r1012;   // flag
    return mb_write_multiple(bp, 0x00, 0x03E9, vals, 26);
}

// DEBUG: Reg-1001-Broadcast mit EINER gesetzten Position (Sweep über alle 26 Register).
esp_err_t fan_bcast1001_pos(int bus, int pos, int val)
{
    const mb_bus_t *bp = (bus == 0) ? &s_sensor_bus : &s_fan_bus;
    uint16_t vals[26] = {0};
    if (pos >= 0 && pos < 26) vals[pos] = (uint16_t)val;
    return mb_write_multiple(bp, 0x00, 0x03E9, vals, 26);
}

// DEBUG: Roh-Frame auf einem Bus mit eigener Baudrate senden (Clip-Fan-RE @ 9600).
int fan_raw(int bus, int baud, const uint8_t *payload, int len, uint8_t *resp, int resp_max)
{
    const mb_bus_t *bp = (bus == 0) ? &s_sensor_bus : &s_fan_bus;
    s_scan_busy = true;                 // regulären Poll kurz pausieren (Baud-Wechsel!)
    vTaskDelay(pdMS_TO_TICKS(40));
    int n = mb_send_raw(bp, (uint32_t)baud, payload, (size_t)len, resp, (size_t)resp_max);
    s_scan_busy = false;
    return n;
}

float vpd_kpa(float t, float rh)
{
    // Sättigungsdampfdruck (Tetens) minus aktueller Dampfdruck
    float svp = 0.6108f * expf((17.27f * t) / (t + 237.3f));
    return svp * (1.0f - rh / 100.0f);
}

// Absolute Feuchte in g/m³ aus Temperatur (°C) + relativer Feuchte (%).
// Für den fairen Vergleich zweier Lufträume (rH% allein wäre temperaturabhängig).
float abs_humidity_gm3(float t, float rh)
{
    float es = 6.112f * expf((17.67f * t) / (t + 243.5f));   // hPa
    return 216.7f * (rh / 100.0f * es) / (273.15f + t);
}
