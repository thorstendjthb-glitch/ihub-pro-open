// ds1302.h — RTC DS1302 (3-Draht bit-bang). Backup-Zeitquelle bei WLAN-Ausfall.
#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t ds1302_init(void);
bool      ds1302_get(struct tm *out);   // liest RTC → tm
esp_err_t ds1302_set(const struct tm *t); // schreibt tm → RTC
