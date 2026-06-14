// nvsutil.h — kleine NVS-Helfer: kapseln open/commit/close in EINEM Aufruf.
// Persistenz-Pfade speichern ihre manuellen Overrides als EIN gepacktes Struct (ein Blob,
// ein Commit) statt vieler Einzel-Keys → weniger Boilerplate (vorher pro Modul dupliziert)
// und schonender für den Flash.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Blob schreiben (open RW → set → commit → close). true bei Erfolg.
bool nvsu_save(const char *ns, const char *key, const void *data, size_t len);

// Blob lesen — NUR wenn der gespeicherte Blob exakt `len` Bytes hat (sonst false,
// `data` bleibt unverändert → Caller-Defaults greifen). So sind FW-Updates, die das
// Struct vergrößern, automatisch sicher (alter, kürzerer Blob wird ignoriert).
bool nvsu_load(const char *ns, const char *key, void *data, size_t len);

// Blob lesen, Teil-Laden erlaubt: ein ÄLTERER (kürzerer) Blob füllt nur den Anfang,
// neue Felder am Ende behalten ihren Default (vorwärtskompatible Struct-Erweiterung).
// `found` (optional) erhält die gespeicherte Größe. false wenn fehlt oder größer als maxlen.
bool nvsu_load_partial(const char *ns, const char *key, void *data, size_t maxlen, size_t *found);

// Gespeicherte Blob-Größe abfragen (0 = Key fehlt) — z. B. um bei Größen-Mismatch
// eine aussagekräftige Warnung zu loggen, bevor Defaults greifen.
size_t nvsu_blob_size(const char *ns, const char *key);

// Skalar-Helfer: ein Key, ein Aufruf. set = open RW → set → commit → close;
// get = open RO → get → close, bei Fehler bleibt *v unverändert (Caller-Default greift).
bool nvsu_set_u8 (const char *ns, const char *key, uint8_t  v);
bool nvsu_get_u8 (const char *ns, const char *key, uint8_t  *v);
bool nvsu_set_i8 (const char *ns, const char *key, int8_t   v);
bool nvsu_get_i8 (const char *ns, const char *key, int8_t   *v);
bool nvsu_set_u16(const char *ns, const char *key, uint16_t v);
bool nvsu_get_u16(const char *ns, const char *key, uint16_t *v);
bool nvsu_set_u32(const char *ns, const char *key, uint32_t v);
bool nvsu_get_u32(const char *ns, const char *key, uint32_t *v);
