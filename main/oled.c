// oled.c — SSD1306 128x64 I2C-Statusdisplay für den iHub-Pro.
//
// DEAKTIVIERBAR über board_pins.h → OLED_ENABLED. Bei 0 werden GPIO1/2
// und der I2C-Bus NICHT angefasst (alle Funktionen sind dann No-Ops).
//
// Verwendet den neuen i2c_master-Treiber (ESP-IDF 5.x). Zeigt Uhrzeit,
// Tag/Nacht und je Kammer Profil/Temp/rH/VPD + Abluft sowie Alarme.
#include "oled.h"
#include "board_pins.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#if OLED_ENABLED
#include "driver/i2c_master.h"
#include "climate_control.h"
#endif

static const char *TAG = "oled";

#if !OLED_ENABLED
// ── Display deaktiviert: alles No-Op, GPIO1/2 bleiben unberührt ──
esp_err_t oled_init(void)   { ESP_LOGI(TAG, "OLED deaktiviert (OLED_ENABLED=0) — GPIO1/2 unberührt"); return ESP_OK; }
void      oled_start(void)  { }
bool      oled_is_active(void) { return false; }
#else

#define OLED_W 128
#define OLED_H 64
#define OLED_PAGES (OLED_H / 8)

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool    s_active;
static uint8_t s_fb[OLED_W * OLED_PAGES];   // 1 Bit/Pixel, page-organisiert

// ── 5x7-Font (column-major, ASCII 0x20..0x7F), Standard-GLCD ──
static const uint8_t FONT5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x7F,0x41,0x41,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x00,0x41,0x41,0x7F,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
    {0x00,0x01,0x02,0x04,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x0C,0x52,0x52,0x52,0x3E}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x7F,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z
    {0x00,0x08,0x36,0x41,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00}, // }
    {0x08,0x04,0x08,0x10,0x08}, // ~
    {0x00,0x00,0x00,0x00,0x00}, // 0x7F
};

static esp_err_t oled_cmd(uint8_t c)
{
    uint8_t b[2] = { 0x00, c };   // 0x00 = Control-Byte "Kommando"
    return i2c_master_transmit(s_dev, b, 2, 100);
}

// ── Framebuffer-Zeichnen (kein I2C) ──
static void fb_clear(void) { memset(s_fb, 0, sizeof(s_fb)); }

// Zeichen an Pixelspalte x, Textzeile page (0..7). Liefert neue x-Position.
static int fb_putc(int x, int page, char ch)
{
    if (page < 0 || page >= OLED_PAGES) return x;
    if (ch < 0x20 || ch > 0x7F) ch = '?';
    const uint8_t *g = FONT5x7[ch - 0x20];
    for (int col = 0; col < 5; col++) {
        int px = x + col;
        if (px >= 0 && px < OLED_W) s_fb[page * OLED_W + px] = g[col];
    }
    int sp = x + 5;                               // 1px Zeichenabstand
    if (sp >= 0 && sp < OLED_W) s_fb[page * OLED_W + sp] = 0x00;
    return x + 6;
}

static void fb_text(int x, int page, const char *s)
{
    while (*s && x < OLED_W) x = fb_putc(x, page, *s++);
}

// Volle Page mit Linie (Trenner) füllen
static void fb_hline(int page, uint8_t pattern)
{
    if (page < 0 || page >= OLED_PAGES) return;
    memset(&s_fb[page * OLED_W], pattern, OLED_W);
}

// Framebuffer → Display (page-weise, je 128 Datenbytes)
static void fb_flush(void)
{
    oled_cmd(0x21); oled_cmd(0x00); oled_cmd(0x7F);   // Spalten 0..127
    oled_cmd(0x22); oled_cmd(0x00); oled_cmd(0x07);   // Pages 0..7
    uint8_t line[1 + OLED_W];
    line[0] = 0x40;                                   // Control-Byte "Daten"
    for (int p = 0; p < OLED_PAGES; p++) {
        memcpy(&line[1], &s_fb[p * OLED_W], OLED_W);
        if (i2c_master_transmit(s_dev, line, sizeof(line), 100) != ESP_OK) return;
    }
}

static esp_err_t ssd1306_boot(void)
{
    static const uint8_t init_seq[] = {
        0xAE,              // Display aus
        0xD5, 0x80,        // Taktteiler
        0xA8, 0x3F,        // Multiplex 1/64
        0xD3, 0x00,        // Display-Offset 0
        0x40,              // Startzeile 0
        0x8D, 0x14,        // Charge-Pump an
        0x20, 0x00,        // Memory-Mode: horizontal
        0xA1,              // Segment-Remap (Spalte 127 → SEG0)
        0xC8,              // COM-Scan absteigend
        0xDA, 0x12,        // COM-Pin-Konfig
        0x81, 0xCF,        // Kontrast
        0xD9, 0xF1,        // Pre-Charge
        0xDB, 0x40,        // VCOMH-Pegel
        0xA4,              // Anzeige aus RAM
        0xA6,              // nicht invertiert
        0xAF,              // Display an
    };
    for (size_t i = 0; i < sizeof(init_seq); i++) {
        esp_err_t e = oled_cmd(init_seq[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

esp_err_t oled_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (e != ESP_OK) { ESP_LOGW(TAG, "I2C-Bus fehlgeschlagen: %s", esp_err_to_name(e)); return e; }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_HZ,
    };
    e = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (e != ESP_OK) { ESP_LOGW(TAG, "I2C-Device fehlgeschlagen: %s", esp_err_to_name(e)); return e; }

    // Display antwortet? (kurzes Probe)
    if (i2c_master_probe(s_bus, OLED_I2C_ADDR, 100) != ESP_OK)
        ESP_LOGW(TAG, "kein ACK von 0x%02X — Modul angeschlossen? Adresse 0x3C/0x3D?", OLED_I2C_ADDR);

    if (ssd1306_boot() != ESP_OK) { ESP_LOGW(TAG, "SSD1306-Init fehlgeschlagen"); return ESP_FAIL; }

    fb_clear();
    fb_text(0, 3, "  iHub-Pro by THB");
    fb_flush();
    s_active = true;
    ESP_LOGI(TAG, "OLED aktiv (SDA=%d SCL=%d Adr=0x%02X)", OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_ADDR);
    return ESP_OK;
}

bool oled_is_active(void) { return s_active; }

// Kurzes Phasen-Kürzel (max 4 Zeichen) fürs Display
static const char *phase_abbr(uint8_t p)
{
    switch (p) {
        case PHASE_SEEDS:  return "Seed";
        case PHASE_CLONES: return "Clon";
        case PHASE_VEG:    return "Veg";
        case PHASE_FLOWER: return "Flwr";
        case PHASE_AUTO:   return "Auto";
        case PHASE_DRY:    return "Dry";
        default:           return "Off";
    }
}

static void render(void)
{
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);

    climate_status_t cs; climate_get_status(&cs);
    chamber_state_t A, B;
    climate_chamber(0, &A);
    climate_chamber(1, &B);

    char buf[24];
    fb_clear();

    // Kopf: Name + Uhr + Tag/Nacht
    snprintf(buf, sizeof(buf), "iHub %02d:%02d %s",
             tm.tm_hour, tm.tm_min, cs.is_day ? "Tag" : "Nacht");
    fb_text(0, 0, buf);
    fb_hline(1, 0x10);   // dünne Trennlinie (1 Pixelzeile gesetzt)

    // Kammer A (Pages 2+3)
    if (A.valid) {
        snprintf(buf, sizeof(buf), "A %-4s %4.1fC %2.0f%%",
                 phase_abbr(A.phase), A.temp, A.rh);
        fb_text(0, 2, buf);
        snprintf(buf, sizeof(buf), "  VPD %.2f F%d%%", A.vpd, A.ifan_pct);
        fb_text(0, 3, buf);
    } else {
        snprintf(buf, sizeof(buf), "A %-4s  --", phase_abbr(A.phase));
        fb_text(0, 2, buf);
    }

    // Kammer B (Pages 4+5)
    if (B.valid) {
        snprintf(buf, sizeof(buf), "B %-4s %4.1fC %2.0f%%",
                 phase_abbr(B.phase), B.temp, B.rh);
        fb_text(0, 4, buf);
        snprintf(buf, sizeof(buf), "  VPD %.2f F%d%%", B.vpd, B.ifan_pct);
        fb_text(0, 5, buf);
    } else {
        snprintf(buf, sizeof(buf), "B %-4s  --", phase_abbr(B.phase));
        fb_text(0, 4, buf);
    }

    fb_hline(6, 0x10);

    // Fußzeile: Alarme (Page 7)
    bool al = cs.alarm_temp || cs.alarm_mold || cs.alarm_co2 || cs.alarm_sensor;
    if (al) {
        snprintf(buf, sizeof(buf), "! ALARM%s%s%s%s",
                 cs.alarm_temp   ? " Temp" : "",
                 cs.alarm_mold   ? " rH"   : "",
                 cs.alarm_co2    ? " CO2"  : "",
                 cs.alarm_sensor ? " Sens" : "");
        fb_text(0, 7, buf);
    } else {
        fb_text(0, 7, "Status OK");
    }

    fb_flush();
}

static void oled_task(void *arg)
{
    while (true) {
        render();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void oled_start(void)
{
    if (!s_active) { ESP_LOGW(TAG, "Render-Task nicht gestartet (Display inaktiv)"); return; }
    xTaskCreate(oled_task, "oled", 3072, NULL, 2, NULL);
}

#endif // OLED_ENABLED
