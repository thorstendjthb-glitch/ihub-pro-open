// nvsutil.c — siehe nvsutil.h
#include "nvsutil.h"
#include "nvs.h"

bool nvsu_save(const char *ns, const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, key, data, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

bool nvsu_load(const char *ns, const char *key, void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = 0;
    bool ok = (nvs_get_blob(h, key, NULL, &sz) == ESP_OK && sz == len &&
               nvs_get_blob(h, key, data, &sz) == ESP_OK);
    nvs_close(h);
    return ok;
}

bool nvsu_load_partial(const char *ns, const char *key, void *data, size_t maxlen, size_t *found)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = 0;
    bool ok = (nvs_get_blob(h, key, NULL, &sz) == ESP_OK && sz > 0 && sz <= maxlen);
    if (ok) {
        size_t len = maxlen;
        ok = (nvs_get_blob(h, key, data, &len) == ESP_OK);
        if (ok && found) *found = sz;
    }
    nvs_close(h);
    return ok;
}

size_t nvsu_blob_size(const char *ns, const char *key)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t sz = 0;
    if (nvs_get_blob(h, key, NULL, &sz) != ESP_OK) sz = 0;
    nvs_close(h);
    return sz;
}

// Ein Makro erzeugt set/get-Paar je Skalartyp (identischer Ablauf, nur andere nvs_*-Calls).
#define NVSU_SCALAR(suffix, type, nvs_setter, nvs_getter)                       \
bool nvsu_set_##suffix(const char *ns, const char *key, type v)                 \
{                                                                               \
    nvs_handle_t h;                                                             \
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;                \
    esp_err_t e = nvs_setter(h, key, v);                                        \
    if (e == ESP_OK) e = nvs_commit(h);                                         \
    nvs_close(h);                                                               \
    return e == ESP_OK;                                                         \
}                                                                               \
bool nvsu_get_##suffix(const char *ns, const char *key, type *v)                \
{                                                                               \
    nvs_handle_t h;                                                             \
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;                 \
    bool ok = (nvs_getter(h, key, v) == ESP_OK);                                \
    nvs_close(h);                                                               \
    return ok;                                                                  \
}

NVSU_SCALAR(u8,  uint8_t,  nvs_set_u8,  nvs_get_u8)
NVSU_SCALAR(i8,  int8_t,   nvs_set_i8,  nvs_get_i8)
NVSU_SCALAR(u16, uint16_t, nvs_set_u16, nvs_get_u16)
NVSU_SCALAR(u32, uint32_t, nvs_set_u32, nvs_get_u32)
