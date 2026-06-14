// relays.c — Steuerung der 10 AC-Relais des iHub-Pro
#include "relays.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvsutil.h"

static const char *TAG = "relays";

// Mapping logischer Index → GPIO + Name. Reihenfolge == relay_id_t.
static const struct {
    gpio_num_t gpio;
    const char *name;
} k_relays[RLY_MAX] = {
    [RLY_LIGHT1]     = { RELAY_LIGHT1,     "light1"     },
    [RLY_LIGHT2]     = { RELAY_LIGHT2,     "light2"     },
    [RLY_HUMIDIFIER] = { RELAY_HUMIDIFIER, "humidifier" },
    [RLY_DEHUMID]    = { RELAY_DEHUMID,    "dehumidifier" },
    [RLY_WATERING]   = { RELAY_WATERING,   "watering"   },
    [RLY_FAN]        = { RELAY_FAN,        "fan"        },
    [RLY_INLINEFAN]  = { RELAY_INLINEFAN,  "inline_fan" },
    [RLY_HEATING]    = { RELAY_HEATING,    "heating"    },
    [RLY_DEVICE1]    = { RELAY_DEVICE1,    "device1"    },
    [RLY_DEVICE2]    = { RELAY_DEVICE2,    "device2"    },
};

static bool s_state[RLY_MAX];
static relay_mode_t s_mode[RLY_MAX];   // AUTO oder MANUAL (User-Override)
static bool s_sched_claim[RLY_MAX];    // true = Zeitplan steuert → relay_set_auto no-op

// Funktions-Zuordnung: Funktion → physische Steckdose (-1 = nicht belegt).
// Default Identität.
static int8_t s_fn_relay[FN_MAX] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

// ── 2-Kammer-Zuordnung (Default: alles Kammer A, Rolle = Identität) ──
static int8_t  s_relay_ch[RLY_MAX]   = { 0,0,0,0,0,0,0,0,0,0 };
static uint8_t s_relay_role[RLY_MAX] = { 0,1,2,3,4,5,6,7,8,9 };  // func_id_t je Steckdose
static int8_t  s_dim_ch[2]           = { 0, 1 };                  // Licht1→Kammer A, Licht2→Kammer B
static int8_t  s_ifan_ch             = 0;                         // Abluft → Kammer A

static void assign_load(void)
{
    nvsu_load_partial("relays", "rch",   s_relay_ch,   sizeof(s_relay_ch),   NULL);
    nvsu_load_partial("relays", "rrole", s_relay_role, sizeof(s_relay_role), NULL);
    nvsu_load_partial("relays", "dch",   s_dim_ch,     sizeof(s_dim_ch),     NULL);
    nvsu_get_i8("relays", "ifch", &s_ifan_ch);
}

static void assign_save(void)
{
    nvsu_save("relays", "rch",   s_relay_ch,   sizeof(s_relay_ch));
    nvsu_save("relays", "rrole", s_relay_role, sizeof(s_relay_role));
    nvsu_save("relays", "dch",   s_dim_ch,     sizeof(s_dim_ch));
    nvsu_set_i8("relays", "ifch", s_ifan_ch);
}

// Manuelle Steckdosen-Overrides (Modus + Zustand) als EIN gepacktes Blob in NVS — NUR bei
// echter Änderung geschrieben (relay_set_auto aus der Regelschleife speichert NICHT → kein
// Flash-Wear), beim Boot wiederhergestellt.
typedef struct { uint8_t mode[RLY_MAX]; uint8_t state[RLY_MAX]; } relay_persist_t;

static void manual_save(void)
{
    relay_persist_t rp;
    for (int i = 0; i < RLY_MAX; i++) { rp.mode[i] = (uint8_t)s_mode[i]; rp.state[i] = s_state[i] ? 1 : 0; }
    nvsu_save("relays", "manual", &rp, sizeof(rp));
}

int8_t    relay_chamber(relay_id_t id) { return id < RLY_MAX ? s_relay_ch[id] : -1; }
func_id_t relay_role(relay_id_t id)    { return id < RLY_MAX ? (func_id_t)s_relay_role[id] : FN_MAX; }

void relay_assign(relay_id_t id, int8_t chamber, func_id_t role)
{
    if (id >= RLY_MAX) return;
    if (chamber < -1 || chamber >= N_CHAMBERS) chamber = -1;
    s_relay_ch[id] = chamber;
    if (role < FN_MAX) s_relay_role[id] = role;
    assign_save();
}

int relay_find(int chamber, func_id_t role)
{
    for (int i = 0; i < RLY_MAX; i++)
        if (s_relay_ch[i] == chamber && s_relay_role[i] == role) return i;
    return -1;
}

int relay_role_find(func_id_t role)
{
    for (int i = 0; i < RLY_MAX; i++)
        if (s_relay_role[i] == role) return i;
    return -1;
}

int8_t dim_chamber(int ch) { return (ch == 0 || ch == 1) ? s_dim_ch[ch] : -1; }
void   dim_set_chamber(int ch, int8_t chamber) { if (ch==0||ch==1){ s_dim_ch[ch]=chamber; assign_save(); } }
int8_t ifan_chamber(void) { return s_ifan_ch; }
void   ifan_set_chamber(int8_t chamber) { s_ifan_ch = chamber; assign_save(); }
static const char *k_func_names[FN_MAX] = {
    "light1", "light2", "humidifier", "dehumidifier", "watering",
    "fan", "inline_fan", "heating", "device1", "device2"
};

static void fnmap_load(void)
{
    nvsu_load("relays", "fnmap", s_fn_relay, sizeof(s_fn_relay));
}

static void fnmap_save(void)
{
    nvsu_save("relays", "fnmap", s_fn_relay, sizeof(s_fn_relay));
}

esp_err_t relay_fn_set_auto(func_id_t fn, bool on)
{
    if (fn >= FN_MAX) return ESP_ERR_INVALID_ARG;
    int8_t p = s_fn_relay[fn];
    if (p < 0 || p >= RLY_MAX) return ESP_OK;   // nicht belegt → nichts tun
    return relay_set_auto((relay_id_t)p, on);
}

int8_t relay_fn_get(func_id_t fn) { return (fn < FN_MAX) ? s_fn_relay[fn] : -1; }

void relay_fn_set(func_id_t fn, int8_t phys)
{
    if (fn >= FN_MAX) return;
    if (phys < -1 || phys >= RLY_MAX) phys = -1;
    s_fn_relay[fn] = phys;
    fnmap_save();
    ESP_LOGI(TAG, "Funktion %s -> Steckdose %d", k_func_names[fn], phys);
}

const char *func_name(func_id_t fn) { return (fn < FN_MAX) ? k_func_names[fn] : "?"; }

static void apply(relay_id_t id, bool on)
{
    gpio_set_level(k_relays[id].gpio, on ? 1 : 0);
    s_state[id] = on;
}

esp_err_t relays_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < RLY_MAX; i++) {
        mask |= (1ULL << k_relays[i].gpio);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Alle Relais sicher AUS (auch GPIO3/Strapping bleibt damit LOW)
    for (int i = 0; i < RLY_MAX; i++) {
        gpio_set_level(k_relays[i].gpio, 0);
        s_state[i] = false;
        s_mode[i]  = RMODE_AUTO;
    }
    fnmap_load();   // Funktions-Zuordnung aus NVS (sonst Identität)
    assign_load();  // 2-Kammer-Zuordnung (Kammer + Rolle je Aktor)

    // Manuelle Steckdosen-Overrides wiederherstellen (überleben Reboot). AUTO-Steckdosen
    // bleiben aus und werden vom Regel-Task gestellt; nur MANUELL gesetzte werden angewandt.
    // AUSNAHME Bewässerung (RLY_WATERING/GPIO3): nie automatisch beim Boot einschalten
    // (Überflutungsschutz + GPIO3 ist Strapping-Pin → bleibt LOW). Bleibt AUTO/AUS.
    relay_persist_t rp;
    if (nvsu_load("relays", "manual", &rp, sizeof(rp)))
        for (int i = 0; i < RLY_MAX; i++)
            if (rp.mode[i] == RMODE_MANUAL && i != RLY_WATERING) {
                s_mode[i] = RMODE_MANUAL;
                apply((relay_id_t)i, rp.state[i] != 0);
            }
    ESP_LOGI(TAG, "10 Relais initialisiert (manuelle Overrides wiederhergestellt, Watering ausgenommen)");
    return ESP_OK;
}

esp_err_t relay_set_manual(relay_id_t id, bool on)
{
    if (id >= RLY_MAX) return ESP_ERR_INVALID_ARG;
    bool changed = (s_mode[id] != RMODE_MANUAL) || (s_state[id] != on);
    s_mode[id] = RMODE_MANUAL;
    apply(id, on);
    if (changed) manual_save();   // nur bei echter Änderung persistieren (Flash schonen)
    ESP_LOGI(TAG, "%s -> %s (MANUELL)", k_relays[id].name, on ? "AN" : "AUS");
    return ESP_OK;
}

esp_err_t relay_set_auto(relay_id_t id, bool on)
{
    if (id >= RLY_MAX) return ESP_ERR_INVALID_ARG;
    if (s_mode[id] != RMODE_AUTO) return ESP_OK;   // User-Override hat Vorrang
    if (s_sched_claim[id]) return ESP_OK;          // Zeitplan besitzt diese Steckdose
    if (s_state[id] != on) {
        apply(id, on);
        ESP_LOGI(TAG, "%s -> %s (auto)", k_relays[id].name, on ? "AN" : "AUS");
    }
    return ESP_OK;
}

void relay_sched_claim(relay_id_t id, bool claimed)
{
    if (id < RLY_MAX) s_sched_claim[id] = claimed;
}

esp_err_t relay_set_sched(relay_id_t id, bool on)
{
    if (id >= RLY_MAX) return ESP_ERR_INVALID_ARG;
    if (s_mode[id] != RMODE_AUTO) return ESP_OK;   // Manuell gewinnt auch gegen Zeitplan
    if (s_state[id] != on) {
        apply(id, on);
        ESP_LOGI(TAG, "%s -> %s (Zeitplan)", k_relays[id].name, on ? "AN" : "AUS");
    }
    return ESP_OK;
}

void relay_set_mode(relay_id_t id, relay_mode_t mode)
{
    if (id >= RLY_MAX) return;
    bool changed = (s_mode[id] != mode);
    s_mode[id] = mode;
    if (changed) manual_save();   // Moduswechsel (z. B. zurück auf AUTO) nur bei Änderung persistieren
    ESP_LOGI(TAG, "%s Modus -> %s", k_relays[id].name, mode == RMODE_AUTO ? "AUTO" : "MANUELL");
}

relay_mode_t relay_get_mode(relay_id_t id)
{
    return (id < RLY_MAX) ? s_mode[id] : RMODE_AUTO;
}

bool relay_get(relay_id_t id)
{
    if (id >= RLY_MAX) return false;
    return s_state[id];
}

const char *relay_name(relay_id_t id)
{
    if (id >= RLY_MAX) return "?";
    return k_relays[id].name;
}
