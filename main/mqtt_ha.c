// mqtt_ha.c — MQTT-Client mit Home-Assistant-Auto-Discovery
#include "mqtt_ha.h"
#include "app_config.h"
#include "appcfg.h"
#include "relays.h"
#include "dimmer.h"
#include "devices.h"
#include "climate_control.h"
#include "water.h"
#include "state.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "mqtt_ha";
static esp_mqtt_client_handle_t s_client;

// Zuletzt publizierte Output-Zustände (-1 = unbekannt → beim Connect alles publishen).
// Grundlage des Diff-Publishings in mqtt_ha_publish_outputs().
static int8_t  s_pub_relay[RLY_MAX];
static int16_t s_pub_light[DIM_COUNT];

#define BASE  CFG_HA_PREFIX
#define DEV   CFG_DEVICE_ID

// Gemeinsamer device-Block für alle Discovery-Payloads (HA gruppiert dann).
#define DEV_JSON \
    "\"dev\":{\"ids\":[\"" DEV "\"],\"name\":\"" CFG_DEVICE_NAME \
    "\",\"mf\":\"Mars Hydro\",\"mdl\":\"iHub-Pro\"}"

static void mqtt_ha_publish_controls(void);   // fwd

// ── Command-Topics ──
// switch:  ihub_pro/relay/<name>/set        Payload ON/OFF
// light:   ihub_pro/light/<n>/set           Payload Helligkeit 0-100
// select:  ihub_pro/profile/<A|B>/set       Payload Profilname
// button:  ihub_pro/ack/set                 Payload PRESS
// number:  ihub_pro/setpoint/<feld>_<A|B>/set  Payload Zahl
static void on_data(esp_mqtt_event_handle_t e)
{
    char topic[128], payload[64];
    int tl = e->topic_len < 127 ? e->topic_len : 127;
    int pl = e->data_len  < 63  ? e->data_len  : 63;
    memcpy(topic, e->topic, tl); topic[tl] = 0;
    memcpy(payload, e->data, pl); payload[pl] = 0;
    ESP_LOGI(TAG, "RX %s = %s", topic, payload);

    // Wasserwerte von HA (tuya-local → grow/water/<key>): empfangen, in State-Cache.
    // on_data läuft single-threaded im MQTT-Task → statischer Akkumulator ist sicher.
    const char *w = strstr(topic, "grow/water/");
    if (w) {
        static water_data_t s_water_rx;
        if (water_apply_kv(&s_water_rx, w + 11, payload))   // 11 = strlen("grow/water/")
            state_set_water(&s_water_rx);
        return;
    }

    // Relais: .../relay/<id>/set
    const char *r = strstr(topic, "/relay/");
    if (r) {
        int id = atoi(r + 7);
        bool on = (strncmp(payload, "ON", 2) == 0);
        relay_set_manual(id, on);   // HA-Befehl = manueller Override
        mqtt_ha_publish_relay(id, on);
        return;
    }
    // Light: .../light/<ch>/set  (0-100)
    const char *l = strstr(topic, "/light/");
    if (l) {
        int ch = atoi(l + 7);
        uint8_t pct = (uint8_t)atoi(payload);
        dimmer_set_manual(ch, pct);   // manueller Befehl → persistent
        mqtt_ha_publish_light(ch, pct);
        return;
    }
    // Profil je Kammer: .../profile/<A|B>/set  (Payload = Profilname)
    const char *pf = strstr(topic, "/profile/");
    if (pf) {
        int ch = (pf[9] == 'B') ? 1 : 0;
        for (int p = 0; p <= PHASE_OFF; p++)
            if (strcmp(climate_phase_name((grow_phase_t)p), payload) == 0) {
                climate_set_phase(ch, (grow_phase_t)p);
                break;
            }
        mqtt_ha_publish_controls();
        return;
    }
    // Alarm quittieren: .../ack/set
    if (strstr(topic, "/ack/set")) { climate_alarm_ack(); return; }
    // Sollwert: .../setpoint/<feld>_<A|B>/set  (Payload = Zahl)
    const char *sp = strstr(topic, "/setpoint/");
    if (sp) {
        sp += 10;                                  // zeigt auf "<feld>_<A|B>/set"
        int ch = strstr(sp, "_B") ? 1 : 0;
        grow_phase_t ph = climate_get_phase(ch);
        if (ph < PHASE_MAX) {
            phase_setpoints_t *s = climate_setpoints(ph);
            if (s) {
                float val = atof(payload);
                if      (strncmp(sp, "vpd", 3)  == 0) s->vpd_target = val;
                else if (strncmp(sp, "temp", 4) == 0) s->temp_day   = val;
                else if (strncmp(sp, "rh", 2)   == 0) s->rh_day     = val;
                climate_save_setpoints();
            }
        }
        mqtt_ha_publish_controls();
        return;
    }
}

static void publish_discovery(void)
{
    char topic[160], payload[512];

    // 10 Relais als HA-switch
    for (int i = 0; i < RLY_MAX; i++) {
        const char *n = relay_name(i);
        snprintf(topic, sizeof(topic), "%s/switch/%s/%s/config", BASE, DEV, n);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"obj_id\":\"%s_%s\","
            "\"cmd_t\":\"%s/relay/%d/set\",\"stat_t\":\"%s/relay/%d/state\","
            "%s}", n, DEV, n, DEV, n, DEV, i, DEV, i, DEV_JSON);
        esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
        char ct[64];
        snprintf(ct, sizeof(ct), "%s/relay/%d/set", DEV, i);
        esp_mqtt_client_subscribe(s_client, ct, 1);
    }

    // 2 Dimmer-Lights (brightness 0-100). WICHTIG: stat_t (ON/OFF) und bri_stat_t
    // (Zahl) sind GETRENNTE Topics — vorher zeigten beide auf das Zahlen-Topic,
    // HA konnte "30" nicht als ON/OFF deuten → Entity stand dauerhaft auf unknown.
    for (int ch = 0; ch < DIM_COUNT; ch++) {
        snprintf(topic, sizeof(topic), "%s/light/%s/light%d/config", BASE, DEV, ch + 1);
        snprintf(payload, sizeof(payload),
            // uniq_id/obj_id MUSS sich vom Steckdosen-Switch "light1"/"light2" unterscheiden,
            // sonst dedupliziert HA beide Entities (Dimmer-Light verschwand). → "dimlight%d".
            "{\"name\":\"Dimmer Light %d\",\"uniq_id\":\"%s_dimlight%d\",\"obj_id\":\"%s_dimlight%d\","
            "\"cmd_t\":\"%s/light/%d/set\",\"stat_t\":\"%s/light/%d/onstate\","
            "\"bri_cmd_t\":\"%s/light/%d/set\",\"bri_stat_t\":\"%s/light/%d/state\","
            "\"bri_scl\":100,\"on_cmd_type\":\"brightness\",%s}",
            ch + 1, DEV, ch + 1, DEV, ch + 1, DEV, ch, DEV, ch, DEV, ch, DEV, ch, DEV_JSON);
        esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
        static char ct[64]; snprintf(ct, sizeof(ct), "%s/light/%d/set", DEV, ch);
        esp_mqtt_client_subscribe(s_client, ct, 1);
    }

    // Sensoren (device_class + unit + Anzeige-Präzision). obj_id → vorhersehbare entity_id.
    // prec = suggested_display_precision (Nachkommastellen, die HA in allen Frontends anzeigt);
    // -1 = nicht setzen (HA-Default). Verhindert VPD-Werte "ohne Ende Stellen" o. Ä.
    struct { const char *id, *name, *unit, *dc; int prec; } sens[] = {
        // Kammer A (Sensor-Bus-TH3in1)
        {"temperature", "Temperatur A", "°C", "temperature", 1},
        {"humidity",    "Feuchte A",    "%",  "humidity",    1},
        {"vpd",         "VPD A",        "kPa","pressure",     2},
        // Kammer B
        {"temp_b",      "Temperatur B", "°C", "temperature", 1},
        {"rh_b",        "Feuchte B",    "%",  "humidity",    1},
        {"vpd_b",       "VPD B",        "kPa","pressure",     2},
        // gemeinsame Sensorik
        {"co2",         "CO2",          "ppm","carbon_dioxide", 0},
        {"ppfd",        "PPFD",         "µmol/m²/s", NULL,    0},
        {"light_lux",   "Licht",        "lx", "illuminance", 0},
        {"dli",         "DLI",          "mol/m²/d", NULL,     2},
        {"soil_temp",   "Substrat-Temperatur", "°C", "temperature", 1},
        {"soil_moist",  "Substrat-Feuchte", "%", "moisture", 1},
        {"soil_ec",     "Substrat-EC",  "mS/cm", NULL,        2},
        {"attic_temp",  "Quellluft Temperatur", "°C", "temperature", 1},
        {"attic_hum",   "Quellluft Feuchte", "%", "humidity", 1},
        {"ifan",        "Abluft",       "%",  NULL,           0},
        {"phase",       "Profil",       "",   NULL,          -1},
        {"grow_day",    "Grow-Tag",     "d",  NULL,           0},
        {"ac_mode",     "Klima-Modus",  "",   NULL,          -1},
        // BL0940 Energiemessung
        {"voltage",     "Spannung",     "V",   "voltage",     1},
        {"current",     "Strom",        "A",   "current",     2},
        {"power",       "Leistung",     "W",   "power",       1},
        {"energy",      "Energie",      "kWh", "energy",      2},
    };
    for (int i = 0; i < (int)(sizeof(sens)/sizeof(sens[0])); i++) {
        snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", BASE, DEV, sens[i].id);
        int p = snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"obj_id\":\"%s_%s\","
            "\"stat_t\":\"%s/sensor/%s/state\",\"unit_of_meas\":\"%s\",",
            sens[i].name, DEV, sens[i].id, DEV, sens[i].id, DEV, sens[i].id, sens[i].unit);
        if (sens[i].dc)
            p += snprintf(payload + p, sizeof(payload) - p, "\"dev_cla\":\"%s\",", sens[i].dc);
        // Anzeige-Präzision: rundet die Darstellung in HA-UI/App auf feste Nachkommastellen
        if (sens[i].prec >= 0)
            p += snprintf(payload + p, sizeof(payload) - p, "\"sug_dsp_prc\":%d,", sens[i].prec);
        // Messwerte → state_class measurement (HA-Statistiken/Langzeit); Energie → total_increasing
        if (strcmp(sens[i].id, "energy") == 0)
            p += snprintf(payload + p, sizeof(payload) - p, "\"stat_cla\":\"total_increasing\",");
        else if (sens[i].dc)
            p += snprintf(payload + p, sizeof(payload) - p, "\"stat_cla\":\"measurement\",");
        snprintf(payload + p, sizeof(payload) - p, "%s}", DEV_JSON);
        esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
    }

    // Alarme + Tag/Nacht als binary_sensor (Basis für HA-Push-Automationen).
    struct { const char *id, *name, *dc; } bsens[] = {
        {"alarm_temp",   "Alarm Temperatur",   "problem"},
        {"alarm_mold",   "Alarm Feuchte/Schimmel", "problem"},
        {"alarm_co2",    "Alarm CO2",          "problem"},
        {"alarm_sensor", "Alarm Sensor-Ausfall", "problem"},
        {"alarm_light",  "Alarm Lampe",        "problem"},
        {"alarm_time",   "Alarm Uhrzeit ungültig", "problem"},
        {"alarm_water",  "Alarm Wasserwert",   "problem"},
        {"is_day",       "Licht-Phase (Tag)",  "light"},
    };
    for (int i = 0; i < (int)(sizeof(bsens)/sizeof(bsens[0])); i++) {
        snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/%s/config", BASE, DEV, bsens[i].id);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"obj_id\":\"%s_%s\","
            "\"stat_t\":\"%s/sensor/%s/state\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
            "\"dev_cla\":\"%s\",%s}",
            bsens[i].name, DEV, bsens[i].id, DEV, bsens[i].id, DEV, bsens[i].id, bsens[i].dc, DEV_JSON);
        esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
    }

    // Klima-Bedarf als binary_sensor (für HA-Automation → Medion-Klima via Tuya)
    snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/ac_demand/config", BASE, DEV);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"Klima-Bedarf\",\"uniq_id\":\"%s_ac_demand\","
        "\"stat_t\":\"%s/sensor/ac_demand/state\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
        "\"dev_cla\":\"running\",%s}", DEV, DEV, DEV_JSON);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);

    // ── Steuerung (Stufe 2): Profil-select + Sollwert-numbers je Kammer + Quittier-button ──
    const char *phase_opts =
        "\"Seeds\",\"Stecklinge\",\"Wuchs\",\"Bluete\",\"Automatics\",\"Trocknen\",\"Aus\"";
    char ct[80];
    for (int ch = 0; ch < N_CHAMBERS; ch++) {
        char chl = 'A' + ch;
        snprintf(topic, sizeof(topic), "%s/select/%s/profile_%c/config", BASE, DEV, chl);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"Profil %c\",\"uniq_id\":\"%s_profile_%c\",\"obj_id\":\"%s_profile_%c\","
            "\"cmd_t\":\"%s/profile/%c/set\",\"stat_t\":\"%s/profile/%c/state\","
            "\"options\":[%s],%s}",
            chl, DEV, chl, DEV, chl, DEV, chl, DEV, chl, phase_opts, DEV_JSON);
        esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
        snprintf(ct, sizeof(ct), "%s/profile/%c/set", DEV, chl);
        esp_mqtt_client_subscribe(s_client, ct, 1);
    }
    // Quittier-Button
    snprintf(topic, sizeof(topic), "%s/button/%s/ack/config", BASE, DEV);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"Alarm quittieren\",\"uniq_id\":\"%s_ack\",\"obj_id\":\"%s_ack\","
        "\"cmd_t\":\"%s/ack/set\",%s}", DEV, DEV, DEV, DEV_JSON);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
    snprintf(ct, sizeof(ct), "%s/ack/set", DEV);
    esp_mqtt_client_subscribe(s_client, ct, 1);
    // Sollwert-Numbers je Kammer (gelten fürs aktive Profil der Kammer)
    struct { const char *id, *name, *unit; float mn, mx, st; } nums[] = {
        {"vpd",  "VPD-Soll",      "kPa", 0.4f, 2.0f, 0.1f},
        {"temp", "Temp-Soll Tag", "°C",  10.0f, 40.0f, 0.5f},
        {"rh",   "rH-Soll Tag",   "%",   20.0f, 95.0f, 1.0f},
    };
    for (int ch = 0; ch < N_CHAMBERS; ch++) {
        char chl = 'A' + ch;
        for (int k = 0; k < 3; k++) {
            snprintf(topic, sizeof(topic), "%s/number/%s/%s_%c/config", BASE, DEV, nums[k].id, chl);
            snprintf(payload, sizeof(payload),
                "{\"name\":\"%s %c\",\"uniq_id\":\"%s_%s_%c\",\"obj_id\":\"%s_%s_%c\","
                "\"cmd_t\":\"%s/setpoint/%s_%c/set\",\"stat_t\":\"%s/setpoint/%s_%c/state\","
                "\"min\":%.1f,\"max\":%.1f,\"step\":%.1f,\"unit_of_meas\":\"%s\",\"mode\":\"box\",%s}",
                nums[k].name, chl, DEV, nums[k].id, chl, DEV, nums[k].id, chl,
                DEV, nums[k].id, chl, DEV, nums[k].id, chl,
                (double)nums[k].mn, (double)nums[k].mx, (double)nums[k].st, nums[k].unit, DEV_JSON);
            esp_mqtt_client_publish(s_client, topic, payload, 0, 1, true);
            snprintf(ct, sizeof(ct), "%s/setpoint/%s_%c/set", DEV, nums[k].id, chl);
            esp_mqtt_client_subscribe(s_client, ct, 1);
        }
    }

    ESP_LOGI(TAG, "HA-Discovery published");
}

static void on_mqtt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "verbunden");
        publish_discovery();
        esp_mqtt_client_subscribe(s_client, "grow/water/+", 0);   // Wasserwerte von HA empfangen
        // Diff-Cache invalidieren → publish_outputs überträgt einmal ALLE Zustände
        // (deckt auch Reconnects ab, nicht nur den Boot).
        for (int i = 0; i < RLY_MAX; i++) s_pub_relay[i] = -1;
        for (int c = 0; c < DIM_COUNT; c++) s_pub_light[c] = -1;
        mqtt_ha_publish_outputs();
        break;
    case MQTT_EVENT_DATA:
        on_data(e);
        break;
    default: break;
    }
}

esp_err_t mqtt_ha_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = appcfg_mqtt_uri(),
        .credentials.username = appcfg_mqtt_user(),
        .credentials.authentication.password = appcfg_mqtt_pass(),
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_mqtt, NULL);
    return esp_mqtt_client_start(s_client);
}

void mqtt_ha_publish_relay(int id, bool on)
{
    if (!s_client || id < 0 || id >= RLY_MAX) return;
    char t[64]; snprintf(t, sizeof(t), "%s/relay/%d/state", DEV, id);
    esp_mqtt_client_publish(s_client, t, on ? "ON" : "OFF", 0, 1, true);
    s_pub_relay[id] = on ? 1 : 0;
}

void mqtt_ha_publish_light(int ch, uint8_t pct)
{
    if (!s_client || ch < 0 || ch >= DIM_COUNT) return;
    char t[64], v[8];
    snprintf(t, sizeof(t), "%s/light/%d/state", DEV, ch);   // Helligkeit (bri_stat_t)
    snprintf(v, sizeof(v), "%u", pct);
    esp_mqtt_client_publish(s_client, t, v, 0, 1, true);
    snprintf(t, sizeof(t), "%s/light/%d/onstate", DEV, ch); // ON/OFF (stat_t)
    esp_mqtt_client_publish(s_client, t, pct > 0 ? "ON" : "OFF", 0, 1, true);
    s_pub_light[ch] = pct;
}

void mqtt_ha_publish_outputs(void)
{
    if (!s_client) return;
    for (int i = 0; i < RLY_MAX; i++) {
        int8_t on = relay_get((relay_id_t)i) ? 1 : 0;
        if (s_pub_relay[i] != on) mqtt_ha_publish_relay(i, on != 0);
    }
    for (int c = 0; c < DIM_COUNT; c++) {
        uint8_t pct = dimmer_get((dim_ch_t)c);
        if (s_pub_light[c] != (int16_t)pct) mqtt_ha_publish_light(c, pct);
    }
}

void mqtt_ha_publish_sensors(const sensor_data_t *d)
{
    char t[64], v[16];
    #define PUB(id, fmt, val) do { \
        snprintf(t, sizeof(t), "%s/sensor/%s/state", DEV, id); \
        snprintf(v, sizeof(v), fmt, val); \
        esp_mqtt_client_publish(s_client, t, v, 0, 0, false); } while (0)

    if (d->th3in1_valid) {
        PUB("temperature", "%.1f", d->temperature_c);
        PUB("humidity", "%.1f", d->humidity_pct);
        PUB("vpd", "%.2f", vpd_kpa(d->temperature_c, d->humidity_pct));
        PUB("light_lux", "%.0f", d->light_lux);
    }
    if (d->co2_valid)  PUB("co2", "%d", d->co2_ppm);
    if (d->par_valid)  PUB("ppfd", "%d", d->ppfd);
    if (d->substrate_valid) {
        PUB("soil_temp", "%.1f", d->soil_temp_c);
        PUB("soil_moist", "%.1f", d->soil_moist_pct);
        PUB("soil_ec", "%.2f", d->soil_ec);
    }
    // Dachboden/Quellluft = KONFIGURIERTE Referenz (ref_*, je nach Quelle Fan-Bus/BLE/Sensor-Bus),
    // NICHT der Fan-Bus-Rohwert (attic_*). Sonst zeigt HA für den Dachboden denselben Wert wie
    // eine Kammer, die als Quelle den Fan-Bus nutzt. Konsistent mit dem WebUI (das ref_* zeigt).
    if (d->ref_valid) {
        PUB("attic_temp", "%.1f", d->ref_temp_c);
        PUB("attic_hum",  "%.1f", d->ref_humidity_pct);
    }
    #undef PUB
}

// Klima-Status publizieren: Alarme (Push-Basis), Tag/Nacht, Profil, DLI, Abluft,
// Grow-Tag, Kammer-B-Klima + Klima-Bedarf/Modus (Medion-Automation).
void mqtt_ha_publish_climate(const climate_status_t *c)
{
    char t[64], v[24];
    #define PUBC(id, val) do { \
        snprintf(t, sizeof(t), "%s/sensor/%s/state", DEV, id); \
        esp_mqtt_client_publish(s_client, t, val, 0, 0, true); } while (0)

    // Alarme + Tag/Nacht (binary_sensors lesen denselben State-Topic)
    PUBC("alarm_temp",   c->alarm_temp   ? "ON" : "OFF");
    PUBC("alarm_mold",   c->alarm_mold   ? "ON" : "OFF");
    PUBC("alarm_co2",    c->alarm_co2    ? "ON" : "OFF");
    PUBC("alarm_sensor", c->alarm_sensor ? "ON" : "OFF");
    PUBC("alarm_light",  c->alarm_light  ? "ON" : "OFF");
    PUBC("alarm_time",   c->alarm_time   ? "ON" : "OFF");
    PUBC("alarm_water",  c->alarm_water  ? "ON" : "OFF");
    PUBC("is_day",       c->is_day       ? "ON" : "OFF");

    // Klima-Bedarf + Modus (HA-Automation Medion-Klima)
    PUBC("ac_demand", c->ac_demand ? "ON" : "OFF");
    static const char *modes[4] = {"aus", "kuehlen", "entfeuchten", "kuehlen+entf"};
    PUBC("ac_mode", modes[c->ac_mode & 3]);

    // Profil (Kammer A) + DLI + Abluft
    PUBC("phase", climate_phase_name((grow_phase_t)c->phase));
    snprintf(v, sizeof(v), "%.2f", c->dli_today); PUBC("dli", v);
    snprintf(v, sizeof(v), "%d", c->ifan_pct);    PUBC("ifan", v);

    // Grow-Tag (Kammer A): Tage seit Start
    uint32_t gstart; uint16_t gdays;
    climate_get_grow(0, &gstart, &gdays);
    if (gstart > 0) {
        long day = ((long)time(NULL) - (long)gstart) / 86400 + 1;
        if (day < 0) day = 0;
        snprintf(v, sizeof(v), "%ld", day); PUBC("grow_day", v);
    }

    // Kammer B Klima (nur wenn Sensor gültig)
    chamber_state_t b; climate_chamber(1, &b);
    if (b.valid) {
        snprintf(v, sizeof(v), "%.1f", b.temp); PUBC("temp_b", v);
        snprintf(v, sizeof(v), "%.1f", b.rh);   PUBC("rh_b", v);
        snprintf(v, sizeof(v), "%.2f", b.vpd);  PUBC("vpd_b", v);
    }
    #undef PUBC
    mqtt_ha_publish_controls();   // Profil/Sollwert-States aktuell halten (auch bei WebUI-Änderung)
}

// Steuer-States (Profil + Sollwerte je Kammer) publishen — retained, idempotent.
static void mqtt_ha_publish_controls(void)
{
    char t[80], v[16];
    for (int ch = 0; ch < N_CHAMBERS; ch++) {
        char chl = 'A' + ch;
        grow_phase_t ph = climate_get_phase(ch);
        snprintf(t, sizeof(t), "%s/profile/%c/state", DEV, chl);
        esp_mqtt_client_publish(s_client, t, climate_phase_name(ph), 0, 1, true);
        if (ph < PHASE_MAX) {
            phase_setpoints_t *s = climate_setpoints(ph);
            if (s) {
                snprintf(t, sizeof(t), "%s/setpoint/vpd_%c/state", DEV, chl);
                snprintf(v, sizeof(v), "%.2f", s->vpd_target);
                esp_mqtt_client_publish(s_client, t, v, 0, 1, true);
                snprintf(t, sizeof(t), "%s/setpoint/temp_%c/state", DEV, chl);
                snprintf(v, sizeof(v), "%.1f", s->temp_day);
                esp_mqtt_client_publish(s_client, t, v, 0, 1, true);
                snprintf(t, sizeof(t), "%s/setpoint/rh_%c/state", DEV, chl);
                snprintf(v, sizeof(v), "%.0f", s->rh_day);
                esp_mqtt_client_publish(s_client, t, v, 0, 1, true);
            }
        }
    }
}

void mqtt_ha_publish_power(const power_data_t *p)
{
    if (!p->valid) return;
    char t[64], v[16];
    #define PUB(id, fmt, val) do { \
        snprintf(t, sizeof(t), "%s/sensor/%s/state", DEV, id); \
        snprintf(v, sizeof(v), fmt, val); \
        esp_mqtt_client_publish(s_client, t, v, 0, 0, false); } while (0)
    PUB("voltage", "%.1f", p->voltage_v);
    PUB("current", "%.3f", p->current_a);
    PUB("power",   "%.1f", p->power_w);
    PUB("energy",  "%.3f", p->energy_kwh);
    #undef PUB
}
