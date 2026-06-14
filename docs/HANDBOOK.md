# iHub-Pro Open Firmware — Handbook

This is the complete manual for the open-source iHub-Pro firmware: every screen,
every setting, and the logic behind the climate control. If you just want to get
running, read **[Quick start](#1-quick-start)** and **[Security & login](#4-security--login)**
first, then come back here when you want to fine-tune.

> The web UI is available in **German and English** — switch it under
> *Settings → System & Network → Language (Sprache)*. This handbook uses the English
> term with the German on-screen label in *(parentheses)* so you can find every control
> regardless of the selected language.

## Contents

1. [Quick start](#1-quick-start)
2. [The dashboard](#2-the-dashboard)
3. [Climate control — the core idea (VPD)](#3-climate-control--the-core-idea-vpd)
4. [Security & login](#4-security--login)
5. [Grow phases & the grow plan](#5-grow-phases--the-grow-plan)
6. [Recommended values per phase](#6-recommended-values-per-phase)
7. [Light: %, PPFD, ramps, DLI, photoperiod](#7-light--ppfd-ramps-dli-photoperiod)
8. [CO₂](#8-co2)
9. [Actuators & roles](#9-actuators--roles)
10. [Outlets & schedules](#10-outlets--schedules)
11. [Dimmers (0–10 V lights)](#11-dimmers-010-v-lights)
12. [Sensors & calibration](#12-sensors--calibration)
13. [Watering automation](#13-watering-automation)
14. [Water-quality alarms](#14-water-quality-alarms)
15. [Alarms & safety](#15-alarms--safety)
16. [Two-chamber setups](#16-two-chamber-setups)
17. [Home Assistant / MQTT](#17-home-assistant--mqtt)
18. [System & network](#18-system--network)
19. [OTA updates & recovery](#19-ota-updates--recovery)
20. [Backup & restore](#20-backup--restore)
21. [Settings reference (every field)](#21-settings-reference-every-field)
22. [Troubleshooting](#22-troubleshooting)
23. [Glossary](#23-glossary)

---

## 1. Quick start

1. **Flash** the firmware (see the README). On first boot it has no Wi-Fi, so it opens
   an open setup hotspot **`iHub-Pro-Setup`**.
2. Connect to that hotspot; a **captive portal** appears (or open `http://192.168.4.1/wifi`).
   Choose your network, enter the password, save. The device reboots into your Wi-Fi.
3. Open **`http://ihub.local/`** (or the device IP). You'll see the dashboard.
4. Go to **Settings** *(⚙)* → **Security & Login** *(Sicherheit & Login)* and set a
   username + password.
5. (Optional) Set your **MQTT broker** *(MQTT-Broker)* for Home Assistant.
6. Assign your outlets, lights, fans and sensors (see the relevant sections), pick a
   **grow phase** or build a **grow plan**, and you're growing.

There is also a permanent WPA2 hotspot **`iHub`** (password under *Settings → System*)
so you can always reach `http://192.168.4.1/` directly, even off your home network.

---

## 2. The dashboard

The home page shows live tiles, colour-coded against the **ideal band of the active
phase** (green = in range):

- **Temperature, Humidity, VPD** — the climate triad. VPD is computed from temp + RH.
- **CO₂** — ppm (raw sensor value + fresh-air baseline, see §8).
- **PPFD / DLI** — instantaneous light intensity and the day's accumulated light.
- **Substrate** — temperature, moisture, EC (if a substrate sensor is connected).
- **Power / Energy / Voltage / Current** — from the BL0940 meter.
- **Water** — pH, EC, TDS, ORP, temperature, etc. (received from Home Assistant via MQTT).
- **Phase banner** — current phase + day X of Y, and a light-cycle countdown.
- **Plan preview** — the phase timeline when a grow plan is active.
- **Outlets & lights** — live state of all 10 outlets and both dimmers; tap to switch
  or set Auto.

A grey/uncoloured tile means that sensor isn't present or hasn't reported — the
firmware never regulates blindly on a missing sensor.

---

## 3. Climate control — the core idea (VPD)

The controller regulates **VPD (Vapour Pressure Deficit)** rather than humidity alone.
VPD combines temperature and humidity into the single number plants actually "feel":

```
VPD = SVP(T) · (1 − RH/100)          [kPa]
SVP(T) = 0.6108 · exp(17.27·T / (T+237.3))
```

Low VPD = humid/muggy air (transpiration stalls, mould risk). High VPD = dry air
(plants close stomata, growth slows). Each phase has a **VPD target** *(Ziel-VPD)* and
a **deadband** *(Totzone)* so actuators don't chatter:

- **VPD too low (too humid):** exhaust up / dehumidifier on / humidifier off.
- **VPD too high (too dry):** humidifier on / exhaust down.
- **Temperature** is a second lever: heater / exhaust / (light dimming as an emergency
  brake).

> Important: because VPD couples temp and RH non-linearly, your humidity target must
> *match* your VPD target. If the dashboard says "too dry" while temp and RH each look
> fine individually, your RH setpoint doesn't line up with your VPD setpoint — use the
> [recommended values](#6-recommended-values-per-phase), which are internally consistent.

Day vs. night setpoints are derived automatically from the **light cycle** (see §7).

---

## 4. Security & login

Found under **Settings → Security & Login** *(Sicherheit & Login)*.

The device is **open until you configure it**, so you can never lock yourself out on a
fresh flash. There are two independent authentication methods:

### Browser login (recommended for humans)
- Set a **username** *(Benutzername)* and **password** *(Neues Passwort)* + confirm.
- The password is stored **salted + SHA-256 hashed** in NVS — never in plain text, and
  never sent back to the browser.
- After saving you're redirected to **`/login`**; sign in to get a session cookie
  (valid 7 days). Use **Sign out** *(Abmelden)* to end the session.
- To remove the password again, tick **Disable login** *(Login deaktivieren)* and save.

### API token (for automation)
- Set an **API token** *(API-Token)* for Home Assistant, scripts and headless OTA.
- Send it as `?key=<token>` in the URL or as an `Authorization: Bearer <token>` header.
- To remove it, tick **Remove token** *(Token entfernen)* and save.

Setting **either** a password or a token flips the device from "open" to "protected".
Browsers use the login; automation uses the token. Both work at the same time.

> **Threat model.** This is plain HTTP on your LAN — there is no TLS. Keep the device on
> a trusted/IoT network. NVS is not encrypted, so physical USB access = full access.
> The login protects against other people on your network, not against someone holding
> the board.

---

## 5. Grow phases & the grow plan

The controller always runs the setpoints of the **current phase**:

| # | Phase (UI label) | Typical use |
|---|---|---|
| 0 | Seedling *(Seeds)* | germinated seedlings |
| 1 | Clone *(Stecklinge)* | cuttings / rooting |
| 2 | Veg *(Wuchs)* | vegetative growth |
| 3 | Bloom *(Blüte)* | flowering |
| 4 | Autoflower *(Automatics)* | autoflowering plants (fixed long photoperiod) |
| 5 | Dry *(Trocknen)* | post-harvest drying/curing |

You can switch the phase manually, **or** build a **grow plan** *(Grow-Plan)* per
chamber: a list of up to 6 steps `(phase, days)`. Set a **start date/time**, enable the
plan, and the controller advances phases automatically by day count and shows
"day X of Y" plus a timeline preview on the dashboard.

### Autoflower mode
Autoflowers don't switch to 12/12 to bloom — they flower on age. Enable **Autoflower**
*(Automatics/Autoflower)* for a chamber to lock the light schedule to a fixed long
photoperiod (e.g. 20/4 or 18/6) regardless of the phase, while climate setpoints still
follow the plan. Set the autoflower light-hours and start hour in the plan section.

---

## 6. Recommended values per phase

Sensible, **internally consistent** defaults (RH chosen so that VPD(temp, RH) ≈ the VPD
target). Everything is editable per phase.

| Phase | Light | VPD (band) | Temp day/night | RH day/night | PPFD | DLI |
|---|---|---|---|---|---|---|
| Seedling | 18/6 | 0.65 (0.45–0.85) | 24/22 °C | 78 / 75 % | 200 | 12 |
| Clone | 18/6 | 0.55 (0.35–0.75) | 24/22 °C | 82 / 79 % | 150 | 10 |
| Veg | 18/6 | 1.00 (0.80–1.20) | 26/22 °C | 70 / 62 % | 500 | 30 |
| Bloom | 12/12 | 1.40 (1.20–1.60) | 25/20 °C | 56 / 40 % | 800 | 40 |
| Autoflower | 20/4 | 1.10 (0.80–1.40) | 25/22 °C | 65 / 58 % | 500 | 30 |
| Dry | off | ~0.85 | 19/19 °C | 61 / 61 % | – | – |

CO₂ (only with an injector): Veg 1000 / Bloom 1200 / Auto 900 ppm, during the light
period only. Tune everything to your genetics and environment.

---

## 7. Light: %, PPFD, ramps, DLI, photoperiod

**Photoperiod timer.** Each phase has **light-on hours** *(light_on_h)* and a
**light-on time** *(light_start_h)*. Midnight wrap is handled correctly (start 18:00 +
12 h → off at 06:00). This drives the day/night state for climate.

**Brightness — two modes** *(per phase)*:
- **Percent** *(light_mode = %)* — fixed dimmer percentage *(light_pct)*.
- **PPFD** *(light_mode = PPFD)* — the firmware adjusts the dimmer to hit a **PPFD
  target** *(ppfd_target)*, measured by a PAR sensor in the PAR chamber.

**Ramps.** Sunrise/sunset soft dimming over **ramp minutes** *(ramp_min)* protects
plants and shaves the power peak.

**DLI** *(Daily Light Integral, mol/m²/day)* is integrated from PPFD: `DLI = Σ(PPFD·Δt)/1e6`.
Each phase has a **DLI target** *(dli_target)*. An optional **DLI floor** *(dli_floor_pct)*
keeps a minimum brightness so the day still accumulates light.

**Light-leak warning.** During the dark period, if the PAR sensor reads above the
**light-fault threshold** *(light_fault_lux)*, a light-leak alarm fires (important in
bloom, where stray light causes stress/hermies).

---

## 8. CO₂

CO₂ is regulated towards the phase **CO₂ target** *(co2_target)* using a device mapped
to the CO₂/Device role (valve or generator). With **CO₂ only in daylight**
*(co2_only_daylight)* enabled, injection happens only during the light period — plants
only use CO₂ while photosynthesising.

**Baseline.** Some NDIR sensors report a value relative to a calibration point. The
**CO₂ baseline** *(co2_baseline, default 420)* is added to the raw reading so outdoor
fresh air reads ~420 ppm. The dashboard shows both raw and baseline-corrected values.
A **CO₂ max alarm** *(co2_max_alarm)* warns on dangerous levels.

---

## 9. Actuators & roles

The firmware controls climate through **roles** (functions), which you map to physical
outputs (see §10). Typical roles:

- **Exhaust fan** — primary VPD/temperature lever (variable speed via the iFan, or a
  switched outlet).
- **Intake fan** — fresh-air supply.
- **Humidifier** — raises humidity (lowers VPD).
- **Dehumidifier** — lowers humidity (raises VPD).
- **Heater** — raises temperature.
- **AC / cooler (Device1)** — see below.
- **CO₂ valve/generator** — see §8.
- **Circulation/clip fan** — air movement (speed, oscillation, "natural" mode).

**Fan base & max** *(fan_base / fan_max)* set the minimum idle speed and the cap for
the exhaust. **Minimum fan cycle** *(fan_min_cycle_s)* prevents rapid on/off chatter.

**Time-based AC fallback.** When the exhaust runs for **AC delay minutes**
*(ac_delay_min, default 15)* but the temperature still won't drop (e.g. outside air
isn't cool enough), the controller can switch on the **AC** for the AC chamber
*(ac_chamber)* and throttle the exhaust back to base load. The AC latch uses real
temperature/VPD feedback (it checks the air is actually getting cooler).

---

## 10. Outlets & schedules

Ten AC outlets. For each outlet you set:

- **Chamber & role assignment** *(Zuordnung)* — which chamber it belongs to and what it
  does (a role from §9), so climate control can drive it. Unassigned outlets are
  manual-only.
- **Schedule mode** *(Modus)*:
  - **Off** *(Aus)* — manual only.
  - **Daily** *(Tagesplan)* — on/off at fixed clock times *(on/off)*.
  - **Cycle** *(Zyklus)* — repeating on-minutes / off-minutes *(con/coff)*, e.g. for
    pumps or circulation.

On the dashboard each outlet can be **On**, **Off**, or **Auto** (follows its
schedule/role).

---

## 11. Dimmers (0–10 V lights)

Two 0–10 V channels drive dimmable grow lights. Per channel:

- **Chamber assignment** *(dimch)* — which chamber the light belongs to.
- **Invert** *(inv0/inv1)* — some ballasts dim inversely (0 V = full). The Mars Hydro
  lights here need PWM **and** an enable line; invert is on by default for them.
- **Zero-point calibration** *(cal0/cal1)* — the minimum drive value where the light
  just turns on, so 1 % actually produces light.

Brightness is driven by the active phase (percent or PPFD mode, with ramps; §7).

---

## 12. Sensors & calibration

Sensors live on the **RS-485 / RJ12** bus (Modbus-RTU) or come in over **BLE**:

- **Wired (RJ12):** TH3in1 (temp/RH/light), CO₂, PPFD/PAR, substrate (T/moisture/EC).
- **Wireless (BLE):** passive scan of supported climate sensors.

**Sensor sources** *(csrc/cmac)*: for each slot — Chamber A, Chamber B and the
reference/attic slot — choose whether the climate sensor is the wired RJ12 sensor or a
specific **BLE sensor by MAC address**. The settings page lists discovered BLE sensors
to pick from.

**Calibration offsets** *(Sensor-Kalibrierung)*: additive correction per slot,
**temperature ±10 °C** and **RH ±20 %**, applied **before** the VPD calculation. Use it
to match a trusted reference thermo-/hygrometer and to correct drifting sensors.

---

## 13. Watering automation

Optional pump automation *(Bewässerung)*:

- **Mode** *(mode)*: Off, timer-based, or moisture-based.
- **Only during day** *(only_day)* — restrict watering to the light period.
- **Duration** *(duration_s)* — pump run time per watering.
- **Interval** *(interval_h)* — time between waterings (timer mode).
- **Moisture low** *(moist_low)* — substrate-moisture threshold that triggers watering
  (moisture mode).
- **Minimum pause** *(min_pause_min)* — guard against over-watering.
- **Max run** *(water_max_min)* — safety cap on continuous pump run time.

> Pumps are never switched automatically unless you enable a watering mode and map a
> pump outlet. Treat hydro/RDWC pumps with care.

---

## 14. Water-quality alarms

If you feed water metrics into the device from Home Assistant (via MQTT), you can alarm
on them *(Wasserwerte-Alarme)*:

- **Enable** *(water_alarm_en)*.
- **pH min/max** *(water_ph_min / water_ph_max)*.
- **Temp max/min** *(water_temp_max / water_temp_min)*.
- **ORP min** *(water_orp_min)*.

The dashboard colours each water tile against these bounds.

---

## 15. Alarms & safety

- **Over-temperature** *(temp_alarm)* and **minimum-temperature** *(temp_min_alarm)*
  per phase.
- **Mould risk** — high RH with falling temperature (near dew point) in bloom →
  alarm + forces dehumidifier/exhaust.
- **Sensor failure** — if a sensor stops responding, the firmware fails safe (no blind
  regulation) and raises an alarm *(sensor_alarm_en)*.
- **Light leak** — PAR above threshold in the dark period (§7).
- **Temperature lockout** — on overheat, dim the lights *(lockout_dim)* and/or force the
  exhaust to max *(lockout_fan)*.
- **Buzzer** *(buzzer_enable)* with a repeat interval *(alarm_repeat_s)*; alarms are
  also published over MQTT.
- **Alarm log** — the last alarm events are kept on the device and shown on the
  dashboard. Acknowledge to silence.

---

## 16. Two-chamber setups

The firmware supports up to **2 independent chambers** plus a shared reference/attic
slot. Each chamber has its own phase/plan, sensor source, setpoints and assigned
actuators. Shared resources (one variable-speed exhaust, one humidifier) are owned by
the chamber you set as their owner *(ifanch / assignment)*. The **PAR chamber**
*(par_chamber)* and **AC chamber** *(ac_chamber)* select which chamber drives PPFD
regulation and the AC fallback respectively.

If you only grow in one tent, just use Chamber A and ignore Chamber B.

---

## 17. Home Assistant / MQTT

Set the broker under **Settings → MQTT** *(MQTT-Broker)*: URI
(`mqtt://homeassistant.local:1883`), username, password. The device publishes **MQTT
auto-discovery**, so all sensors and controls appear in Home Assistant automatically —
no YAML needed. Water-quality values flow the other way (HA → device) over MQTT.

Leave the broker blank to run completely standalone (the web UI still works).

For HTTP automation (e.g. REST commands), set an **API token** and append
`?key=<token>` — see §4.

---

## 18. System & network

Under **Settings → System & Network** *(System & Netzwerk)*:

- **Language** *(Sprache / Language)* — switch the whole UI between German and English.
  The choice is stored per browser and applies to the dashboard and settings.
- **Hostname (mDNS)** *(Hostname)* — reach the device at `http://<name>.local/`
  (applies after reboot).
- **Timezone** *(Zeitzone)* — POSIX-TZ; drives local time for all schedules. Applies
  immediately, including automatic DST. Several presets plus a custom entry.
- **Hotspot password** *(Hotspot-Passwort)* — the WPA2 password of the always-on `iHub`
  hotspot. Randomly generated per device (shown here, read-only).
- **API protection** *(API-Schutz)* — informational; protection is enforced
  automatically whenever a password or token is set (§4).

Time is synced via **SNTP** (`pool.ntp.org`) with a **DS1302 RTC** as backup, so
schedules keep working through Wi-Fi/power outages.

---

## 19. OTA updates & recovery

- **Settings page (recommended):** *Settings → Firmware update* *(Firmware-Update)* shows
  the current build, lets you pick a `.bin` and **Upload & flash** *(Hochladen & Flashen)*
  with a progress bar, plus a **Reboot** *(Neustart)* button. A logged-in browser needs
  no token.
- **Minimal page:** `http://ihub.local/update` → upload `firmware.bin` (append
  `?key=<token>` if a token is set).
- **curl** (needs a token): `curl --data-binary @firmware.bin "http://ihub.local/ota?key=<TOKEN>"`

**Boot-validator / rollback.** A freshly flashed image is only marked valid after it
runs stably for ~60 s. If a bad image keeps crashing on boot, the device automatically
**rolls back to the previous firmware** — no need to open the case. There are two OTA
slots (A/B) for this.

**Reset Wi-Fi.** Hold the config button (see `buttons.c`) to force the setup AP, or
re-flash. **Factory reset** clears NVS (erases all settings, including the login).

---

## 20. Backup & restore

On the settings page:

- **Export** *(⬇ Export)* — saves all settings as a JSON file. **Passwords, the API
  token and Wi-Fi credentials are NOT included** (secrets are never exported).
- **Import** *(⬆ Import)* — restores settings from such a file.

This is great for cloning a configuration across several devices.

---

## 21. Settings reference (every field)

Grouped as on the settings page. Phase fields apply **per phase**.

### Phase setpoints (per phase)
| Field | Meaning |
|---|---|
| `light_on_h` | Light-on hours per day |
| `light_start_h` | Hour the light turns on |
| `ramp_min` | Sunrise/sunset ramp duration (min) |
| `vpd_target` / `vpd_deadband` | VPD target and hysteresis band (kPa) |
| `temp_day` / `temp_night` / `temp_deadband` | Temperature setpoints + band (°C) |
| `rh_day` / `rh_night` | Humidity setpoints (%) |
| `co2_target` / `co2_only_daylight` | CO₂ target (ppm) / inject only in light |
| `light_pct` | Brightness in percent mode |
| `light_mode` | 0 = percent, 1 = PPFD |
| `ppfd_target` | PPFD target in PPFD mode (µmol/m²/s) |
| `dli_target` | Daily light integral target (mol/m²/day) |
| `temp_alarm` / `temp_min_alarm` | High/low temperature alarm thresholds |
| `rh_alarm` | High humidity alarm threshold |
| `co2_max_alarm` | CO₂ high alarm threshold |

### Global
| Field | Meaning |
|---|---|
| `buzzer_enable` / `alarm_repeat_s` | Buzzer on/off + repeat interval |
| `fan_min_cycle_s` | Minimum fan on/off cycle (anti-chatter) |
| `fan_base` / `fan_max` | Exhaust idle speed / max speed |
| `lockout_dim` / `lockout_fan` | Overheat: dim lights / force exhaust max |
| `sensor_alarm_en` | Alarm on sensor failure |
| `light_fault_lux` | Light-leak threshold in dark period |
| `dli_floor_pct` | Minimum brightness to keep DLI accumulating |
| `ac_delay_min` | Exhaust-run time before AC fallback kicks in |
| `co2_baseline` | Added to raw CO₂ reading (fresh air ≈ 420) |
| `ac_chamber` / `par_chamber` | Chamber for AC fallback / PPFD regulation |
| `hist_save_min` | History flash-save interval (0 = off) |
| `water_max_min` | Pump max continuous run (safety) |
| `water_alarm_en` | Enable water-quality alarms |
| `water_ph_min/max`, `water_temp_min/max`, `water_orp_min` | Water alarm bounds |

### MQTT / Security / System
See [§4](#4-security--login), [§17](#17-home-assistant--mqtt) and
[§18](#18-system--network).

---

## 22. Troubleshooting

- **Can't reach `ihub.local`** — use the IP, or connect to the `iHub` hotspot and open
  `http://192.168.4.1/`. mDNS can be flaky on some networks.
- **Locked out / forgot password** — connect over USB and erase NVS (factory reset), or
  reflash. The device is open again after an NVS erase.
- **Dashboard says "too dry" but temp & RH look fine** — your RH setpoint doesn't match
  your VPD target; use the [recommended values](#6-recommended-values-per-phase) (§3).
- **A sensor tile is grey** — that sensor isn't detected. Check the RJ12 cable / source
  assignment (§12). The firmware won't regulate on a missing sensor.
- **Light won't turn off at midnight** — check the **timezone** (§18); schedules use
  local time.
- **Home Assistant entities missing** — verify the MQTT broker URI/credentials and that
  HA's MQTT integration has discovery enabled.
- **OTA upload stalls near the end over weak Wi-Fi** — retry from a stronger signal or
  the `iHub` hotspot; the boot-validator protects you from a half-written image.

---

## 23. Glossary

- **VPD** — Vapour Pressure Deficit; the "dryness" of the air as plants feel it (kPa).
- **PPFD** — Photosynthetic Photon Flux Density; instantaneous light on the canopy
  (µmol/m²/s).
- **DLI** — Daily Light Integral; total light over a day (mol/m²/day).
- **Photoperiod** — the daily light/dark schedule (e.g. 18/6, 12/12).
- **NVS** — the ESP32's non-volatile storage; where settings and credentials live.
- **OTA** — Over-The-Air firmware update.
- **mDNS** — local name resolution (`ihub.local`).
- **Modbus-RTU** — the serial protocol used on the RS-485 sensor/fan bus.
