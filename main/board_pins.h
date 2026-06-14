// board_pins.h — Mars Hydro iHub-Pro (Mainboard M-ZC-A-V53)
// Vollständiges GPIO-Mapping, per Multimeter verifiziert 2026-06-01.
// MCU: ESP32-S3-WROOM-1-N8R2 (8 MB Flash, 2 MB embedded Quad-PSRAM).
#pragma once
#include "driver/gpio.h"

// ─────────────────────────────────────────────────────────────
// Relais (Direct-GPIO, active-high über SOT-23-Transistor)
// Front-Status-LEDs AC1..AC10 sind hardware-an-Relais gekoppelt.
// ─────────────────────────────────────────────────────────────
#define RELAY_LIGHT1      GPIO_NUM_12
#define RELAY_LIGHT2      GPIO_NUM_13
#define RELAY_HUMIDIFIER  GPIO_NUM_11
#define RELAY_DEHUMID     GPIO_NUM_10
#define RELAY_WATERING    GPIO_NUM_3    // ⚠️ Strapping (JTAG-Sel) — beim Boot LOW lassen
#define RELAY_FAN         GPIO_NUM_14
#define RELAY_INLINEFAN   GPIO_NUM_21
#define RELAY_HEATING     GPIO_NUM_42
#define RELAY_DEVICE1     GPIO_NUM_8
#define RELAY_DEVICE2     GPIO_NUM_9

#define RELAY_COUNT       10

// ─────────────────────────────────────────────────────────────
// RS-485 Sensor-Bus (UART1, MAX3485 U2) — Modbus-RTU 115200 8N1
// /RE fest auf GND (Receiver immer aktiv), nur DE wird gesteuert.
// ─────────────────────────────────────────────────────────────
#define SENSOR_UART_NUM   UART_NUM_1
#define SENSOR_TX_PIN     GPIO_NUM_17
#define SENSOR_RX_PIN     GPIO_NUM_18
#define SENSOR_DE_PIN     GPIO_NUM_7

// ─────────────────────────────────────────────────────────────
// RS-485 Fan-Bus (UART2, MAX3485 U3) — Modbus-RTU 115200 8N1
// ─────────────────────────────────────────────────────────────
#define FAN_UART_NUM      UART_NUM_2
#define FAN_TX_PIN        GPIO_NUM_47
#define FAN_RX_PIN        GPIO_NUM_48
#define FAN_DE_PIN        GPIO_NUM_45   // ⚠️ Strapping (VDD_SPI) — Pull-Up beachten

// ─────────────────────────────────────────────────────────────
// BL0940 Energy-Meter (UART0, über 2 Optokoppler, galv. getrennt)
// 4800 Baud, 8N, 1.5 Stop-Bits, Half-Duplex, Slave.
// GPIO43/44 = Default-Konsole → Firmware-Log über USB-Serial-JTAG!
// ─────────────────────────────────────────────────────────────
// TX/RX gegenüber Default-Konsole gekreuzt verdrahtet (am Gerät verifiziert:
// nur mit TX=44/RX=43 antwortet der BL0940 mit gültigem 0x55-Paket).
#define BL0940_UART_NUM   UART_NUM_0
#define BL0940_TX_PIN     GPIO_NUM_44
#define BL0940_RX_PIN     GPIO_NUM_43
#define BL0940_BAUD       4800

// ─────────────────────────────────────────────────────────────
// 0-10V Dimmer (LEDC-PWM → BL358-OpAmp U6 → 0-10V)
// 2026-06-05 (am Gerät per BL0940-Leistungsmessung verifiziert): Die DIM-Stufe braucht ZWEI
// Pins pro Kanal: GPIO35/37 = PWM-Signal (RC→OpAmp), GPIO38/36 = ENABLE der Stufe (statisch).
// Ohne aktiven Enable kommt das PWM nicht durch → Lampe bleibt auf Default (Vollgas). Genau
// das passierte seit dem OEM-Sniff (Enable verstellt). PWM allein (35/37) wirkte nicht,
// GPIO38/36 als HIGH/LOW dagegen schalteten die Stufe (Leistungssprünge).
// ─────────────────────────────────────────────────────────────
#define DIM1_PWM_PIN      GPIO_NUM_35   // LIGHT1 PWM-Signal
#define DIM2_PWM_PIN      GPIO_NUM_37   // LIGHT2 PWM-Signal
#define DIM1_EN_PIN       GPIO_NUM_38   // LIGHT1 DIM-Stufe Enable (statisch)
#define DIM2_EN_PIN       GPIO_NUM_36   // LIGHT2 DIM-Stufe Enable (statisch)

// ─────────────────────────────────────────────────────────────
// Taster (Input, Pull-Up, active-low)
// ─────────────────────────────────────────────────────────────
#define KEY_AC_PIN        GPIO_NUM_40   // Power-Taster
#define KEY_BT_PIN        GPIO_NUM_39   // Pairing-Taster

// ─────────────────────────────────────────────────────────────
// Status-LEDs (Front): ZWEI grüne LEDs (Power + BT), ANTIPARALLEL zwischen
// GPIO4 und GPIO5 verdrahtet — NICHT gegen Masse!
//   GPIO4=1, GPIO5=0  → Power-LED an
//   GPIO4=0, GPIO5=1  → BT-LED an
//   GPIO4 == GPIO5    → beide aus (keine Spannungsdifferenz)
// → Nur EINE kann gleichzeitig leuchten. Ansteuerung differentiell (status_leds.c).
// Am Gerät per LED-Testmodus 6 (beide HIGH → aus) verifiziert (2026-06-01).
#define LED_POWER_PIN     GPIO_NUM_4
#define LED_BT_PIN        GPIO_NUM_5

// ─────────────────────────────────────────────────────────────
// Buzzer (LEDC-Ton)
// ─────────────────────────────────────────────────────────────
#define BUZZER_PIN        GPIO_NUM_46   // ⚠️ Strapping (Boot)

// ─────────────────────────────────────────────────────────────
// RTC DS1302 (3-Draht bit-bang)
// ─────────────────────────────────────────────────────────────
#define RTC_CE_PIN        GPIO_NUM_41
#define RTC_IO_PIN        GPIO_NUM_16
#define RTC_SCLK_PIN      GPIO_NUM_15

// ─────────────────────────────────────────────────────────────
// OLED-Display (SSD1306 128x64, I2C) — OPTIONAL, ab Werk DEAKTIVIERT.
// SDA=GPIO1, SCL=GPIO2 sind ab Werk durch SMD-Widerstände belegt
// (vermutlich Board-Versionskodierung). ERST diese Widerstände
// entfernen, DANN OLED_ENABLED auf 1 setzen und neu flashen.
// Solange OLED_ENABLED == 0 fasst die Firmware GPIO1/2 NICHT an
// (kein I2C-Bus, kein GPIO-Config) → Pins bleiben Hi-Z-Input.
// ─────────────────────────────────────────────────────────────
#define OLED_ENABLED      0            // 0 = aus (GPIO1/2 unberührt), 1 = aktiv
#define OLED_SDA_PIN      GPIO_NUM_1
#define OLED_SCL_PIN      GPIO_NUM_2
#define OLED_I2C_ADDR     0x3C         // SSD1306 Standard (0x3C); manche Module 0x3D
#define OLED_I2C_HZ       400000       // Fast-Mode

// ─────────────────────────────────────────────────────────────
// Freie Reserve-GPIOs: 0(strap), 6, 33, 34, 35, 37  (35/37 frei seit DIM-Korrektur auf 38/36)
// (1, 2 = OLED-Reserve, siehe oben)
// ─────────────────────────────────────────────────────────────
