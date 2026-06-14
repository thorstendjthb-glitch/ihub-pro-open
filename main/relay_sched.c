// relay_sched.c — siehe relay_sched.h
#include "relay_sched.h"
#include "relays.h"
#include "nvsutil.h"
#include "esp_log.h"

static const char *TAG = "sched";

static relay_sched_t s_sched[RLY_MAX];   // Default: alles mode 0 = kein Zeitplan

static void apply_claims(void)
{
    for (int i = 0; i < RLY_MAX; i++)
        relay_sched_claim((relay_id_t)i, s_sched[i].mode != 0);
}

void relay_sched_init(void)
{
    nvsu_load_partial("relays", "sched", s_sched, sizeof(s_sched), NULL);
    apply_claims();
}

relay_sched_t *relay_sched(int relay)
{
    static relay_sched_t dummy;
    return (relay >= 0 && relay < RLY_MAX) ? &s_sched[relay] : &dummy;
}

void relay_sched_save(void)
{
    nvsu_save("relays", "sched", s_sched, sizeof(s_sched));
    apply_claims();
    ESP_LOGI(TAG, "Zeitpläne gespeichert");
}

// Tagesplan: AN ab on_min bis off_min, mit Mitternachts-Überlauf (wie die Licht-Uhr).
// on == off wäre mehrdeutig → Plan inaktiv (Steckdose bleibt auf letztem Zustand).
static bool plan_on(const relay_sched_t *s, int m)
{
    if (s->on_min == s->off_min) return false;
    if (s->on_min < s->off_min) return (m >= s->on_min && m < s->off_min);
    return (m >= s->on_min || m < s->off_min);   // über Mitternacht
}

void relay_sched_tick(const struct tm *now)
{
    if (now->tm_year < 124) return;              // Systemzeit ungültig → nichts schalten
    int m = now->tm_hour * 60 + now->tm_min;
    for (int i = 0; i < RLY_MAX; i++) {
        const relay_sched_t *s = &s_sched[i];
        if (s->mode == 1) {
            if (s->on_min != s->off_min)
                relay_set_sched((relay_id_t)i, plan_on(s, m));
        } else if (s->mode == 2) {
            int per = s->cyc_on_min + s->cyc_off_min;
            if (per > 0)
                relay_set_sched((relay_id_t)i, (m % per) < s->cyc_on_min);
        }
    }
}
