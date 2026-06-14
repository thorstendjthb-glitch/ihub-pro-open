// app_config.h — Compile-time fallback defaults.
//
// For the open-source release these are intentionally EMPTY. The device is fully
// configurable at RUNTIME and nothing here needs to be edited to build or flash:
//   * Wi-Fi      → first boot opens a setup hotspot "iHub-Pro-Setup" with a
//                  captive portal (pick your network, enter the password).
//   * MQTT/HA    → set the broker in the web UI under Settings → MQTT.
//   * Login/Token→ set in the web UI under Settings → Security.
// All of it is stored in NVS and survives reboots/OTA updates.
//
// OPTIONAL (private builds only): to pre-bake credentials into the firmware,
// copy `secrets.h.example` to `secrets.h` and fill it in. `secrets.h` is listed
// in .gitignore and, if present, overrides the empty defaults below.
// NEVER commit real credentials to a public repository.
#pragma once

#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

#ifndef CFG_WIFI_SSID
#define CFG_WIFI_SSID       ""   // empty → boots into Wi-Fi setup AP + captive portal
#endif
#ifndef CFG_WIFI_PASSWORD
#define CFG_WIFI_PASSWORD   ""
#endif

#ifndef CFG_MQTT_URI
#define CFG_MQTT_URI        ""   // empty → MQTT/Home Assistant stays off until configured
#endif
#ifndef CFG_MQTT_USER
#define CFG_MQTT_USER       ""
#endif
#ifndef CFG_MQTT_PASS
#define CFG_MQTT_PASS       ""
#endif

// Home Assistant discovery prefix + device identity (not secret).
#ifndef CFG_HA_PREFIX
#define CFG_HA_PREFIX       "homeassistant"
#endif
#ifndef CFG_DEVICE_ID
#define CFG_DEVICE_ID       "ihub_pro"
#endif
#ifndef CFG_DEVICE_NAME
#define CFG_DEVICE_NAME     "Mars Hydro iHub-Pro"
#endif
