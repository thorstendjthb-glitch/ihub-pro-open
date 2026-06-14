// status_leds.c — Front-Status-LEDs (Power + BT)
#include "status_leds.h"
#include "board_pins.h"
#include "wifi_conn.h"
#include "state.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Diagnose-Override: 0=normal, 1=nur GPIO4 an, 2=nur GPIO5 an,
// 3=nur GPIO4 blinkt, 4=nur GPIO5 blinkt. Zum eindeutigen Identifizieren der LEDs.
static volatile int s_test = 0;
void status_leds_test(int mode) { s_test = mode; }

// Hardware: ZWEI grüne LEDs (Power + BT) ANTIPARALLEL zwischen GPIO4/GPIO5.
//   (1,0)=Power an · (0,1)=BT an · beide gleich=aus. → Nur eine kann zugleich
//   leuchten. Power-Status und BT-Alarm sind aber UNABHÄNGIG: damit beide
//   gleichzeitig „leuchten", wird zeitmultiplext (schnelles Umschalten, das
//   Auge sieht beide). Power zeigt immer seinen WLAN-Status, unabhängig vom Alarm.
#define TICK_MS      5          // Multiplex-Takt (Frame 4×5 ms = 50 Hz, flimmerfrei)
#define BLINK_TICKS  80         // ~400 ms Blink-Halbperiode (80 × 5 ms)

static void task(void *arg)
{
    uint32_t tick = 0;
    while (true) {
        bool blink = ((tick / BLINK_TICKS) & 1);   // langsames Blinken aus Tick-Zähler

        if (s_test) {
            // 1=GPIO4 an, 2=GPIO5 an, 3=GPIO4 blink, 4=GPIO5 blink,
            // 5=GPIO4 an+GPIO5 blink, 6=BEIDE dauerhaft HIGH, 7=beide aus
            gpio_set_level(LED_POWER_PIN, (s_test == 1 || s_test == 5 || s_test == 6 || (s_test == 3 && blink)) ? 1 : 0);
            gpio_set_level(LED_BT_PIN,    (s_test == 2 || s_test == 6 || (s_test == 4 && blink) || (s_test == 5 && blink)) ? 1 : 0);
            tick++; vTaskDelay(pdMS_TO_TICKS(TICK_MS)); continue;
        }

        bool wifi = wifi_is_connected();
        climate_status_t c;
        state_get_climate(&c);
        bool alarm = c.alarm_mold || c.alarm_temp || c.alarm_co2 || c.alarm_sensor;

        // Power zeigt IMMER nur ihren WLAN-Status (verbunden=an, Suche=blinkt) —
        // unabhängig vom Alarm. Die antiparallele BT-LED wird im Alarm zeitmultiplext
        // dazugemischt: Power belegt konstant 3 von 4 Slices (75% → ruhig, kein Pulsen),
        // Slice 3 trägt den BT-Blink. "Aus" = (1,1), damit die andere LED stromlos bleibt.
        bool power_lit = wifi ? true : blink;   // eigener Power-Status
        int  slice = tick & 3;                  // 0..3
        int  g4 = 1, g5 = 1;                    // (1,1) = aus

        if (alarm) {
            if (slice < 3) {                    // Power-Slices (konstant 75%)
                if (power_lit) { g4 = 1; g5 = 0; }      // Power an
                /* sonst (1,1) = aus → Power-Such-Blink */
            } else {                            // BT-Slice (25%) → blinkt
                if (blink) { g4 = 0; g5 = 1; }          // BT an
            }
        } else {                                // kein Alarm: reine Power-Anzeige
            if (power_lit) { g4 = 1; g5 = 0; }
        }
        gpio_set_level(LED_POWER_PIN, g4);
        gpio_set_level(LED_BT_PIN, g5);

        tick++;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t status_leds_start(void)
{
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << LED_POWER_PIN) | (1ULL << LED_BT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&c);
    gpio_set_level(LED_POWER_PIN, 0);
    gpio_set_level(LED_BT_PIN, 0);
    xTaskCreate(task, "status_leds", 2560, NULL, 3, NULL);
    return ESP_OK;
}
