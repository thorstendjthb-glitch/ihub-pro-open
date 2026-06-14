// ds1302.c — RTC DS1302 (3-Draht bit-bang)
#include "ds1302.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"   // ets_delay_us
#include "esp_log.h"

static const char *TAG = "ds1302";

// Register (Read = Write|0x01)
#define R_SEC   0x80
#define R_MIN   0x82
#define R_HOUR  0x84
#define R_DATE  0x86
#define R_MON   0x88
#define R_DOW   0x8A
#define R_YEAR  0x8C
#define R_WP    0x8E   // Write Protect

static inline void clk(int us) { ets_delay_us(us); }

static void start(void)   { gpio_set_level(RTC_CE_PIN, 1); clk(4); }
static void stop(void)    { gpio_set_level(RTC_CE_PIN, 0); clk(4); }

static void write_byte(uint8_t b)
{
    gpio_set_direction(RTC_IO_PIN, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(RTC_IO_PIN, (b >> i) & 1);
        clk(2);
        gpio_set_level(RTC_SCLK_PIN, 1); clk(2);
        gpio_set_level(RTC_SCLK_PIN, 0); clk(2);
    }
}

static uint8_t read_byte(void)
{
    uint8_t b = 0;
    gpio_set_direction(RTC_IO_PIN, GPIO_MODE_INPUT);
    for (int i = 0; i < 8; i++) {
        if (gpio_get_level(RTC_IO_PIN)) b |= (1 << i);
        gpio_set_level(RTC_SCLK_PIN, 1); clk(2);
        gpio_set_level(RTC_SCLK_PIN, 0); clk(2);
    }
    return b;
}

static uint8_t reg_read(uint8_t reg)
{
    start();
    write_byte(reg | 0x01);
    uint8_t v = read_byte();
    stop();
    return v;
}

static void reg_write(uint8_t reg, uint8_t val)
{
    start();
    write_byte(reg);
    write_byte(val);
    stop();
}

static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

esp_err_t ds1302_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << RTC_CE_PIN) | (1ULL << RTC_SCLK_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&c);
    gpio_set_level(RTC_CE_PIN, 0);
    gpio_set_level(RTC_SCLK_PIN, 0);
    gpio_set_direction(RTC_IO_PIN, GPIO_MODE_OUTPUT);
    reg_write(R_WP, 0x00);   // Write-Protect aus
    ESP_LOGI(TAG, "DS1302 init (CE=%d IO=%d SCLK=%d)", RTC_CE_PIN, RTC_IO_PIN, RTC_SCLK_PIN);
    return ESP_OK;
}

bool ds1302_get(struct tm *o)
{
    uint8_t sec = reg_read(R_SEC);
    if (sec & 0x80) return false;   // Clock-Halt-Bit → RTC läuft nicht / leer
    o->tm_sec  = bcd2dec(sec & 0x7F);
    o->tm_min  = bcd2dec(reg_read(R_MIN));
    o->tm_hour = bcd2dec(reg_read(R_HOUR) & 0x3F);
    o->tm_mday = bcd2dec(reg_read(R_DATE));
    o->tm_mon  = bcd2dec(reg_read(R_MON)) - 1;
    o->tm_year = bcd2dec(reg_read(R_YEAR)) + 100;  // 20xx
    return true;
}

esp_err_t ds1302_set(const struct tm *t)
{
    reg_write(R_WP, 0x00);
    reg_write(R_SEC,  dec2bcd(t->tm_sec) & 0x7F);   // CH=0 → Clock läuft
    reg_write(R_MIN,  dec2bcd(t->tm_min));
    reg_write(R_HOUR, dec2bcd(t->tm_hour));
    reg_write(R_DATE, dec2bcd(t->tm_mday));
    reg_write(R_MON,  dec2bcd(t->tm_mon + 1));
    reg_write(R_YEAR, dec2bcd((t->tm_year - 100) % 100));
    reg_write(R_WP, 0x80);   // Write-Protect wieder an
    return ESP_OK;
}
