// water.c — siehe water.h
#include "water.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

bool water_is_fresh(const water_data_t *w, int max_age_s)
{
    if (w->last_update_us == 0) return false;
    int64_t age = esp_timer_get_time() - w->last_update_us;
    return age < (int64_t)max_age_s * 1000000LL;
}

// "unavailable"/"unknown"/leer → kein gültiger Messwert.
static bool parse_num(const char *s, float *out)
{
    if (!s || !s[0]) return false;
    if (strcmp(s, "unavailable") == 0 || strcmp(s, "unknown") == 0 ||
        strcmp(s, "none") == 0 || strcmp(s, "None") == 0) return false;
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s) return false;             // keine Zahl
    while (*end == ' ') end++;
    if (*end != 0) return false;            // Rest-Müll → ungültig
    *out = v;
    return true;
}

bool water_apply_kv(water_data_t *w, const char *key, const char *payload)
{
    float v; bool ok = parse_num(payload, &v);

    // Makro: Feld + valid-Flag setzen, bei Gültigkeit Zeitstempel auffrischen.
    #define SET(k, field, vflag) \
        if (strcmp(key, k) == 0) { w->vflag = ok; if (ok) { w->field = v; w->last_update_us = esp_timer_get_time(); } return true; }

    SET("ph",       ph,           ph_valid)
    SET("ec",       ec_us,        ec_valid)
    SET("tds",      tds_ppm,      tds_valid)
    SET("orp",      orp_mv,       orp_valid)
    SET("temp",     temp_c,       temp_valid)
    SET("salinity", salinity_ppm, salinity_valid)
    SET("sg",       sg,           sg_valid)
    SET("cf",       cf,           cf_valid)
    #undef SET
    return false;   // unbekannter key
}
