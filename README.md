# iHub-Pro Open Firmware 🌱

Open-source ESP32-S3 firmware that replaces the cloud firmware on the **Mars Hydro
iHub-Pro** 10-outlet smart grow controller. It turns the device into a fully
**local, cloud-free climate computer** for a grow tent or room: VPD-based climate
control, photoperiod timers, CO₂/PPFD/DLI handling, energy metering and native
**Home Assistant** integration over MQTT — all configurable from a built-in web UI,
no app and no account required.

> **Independent project.** "Mars Hydro" and "iHub-Pro" are trademarks of their
> respective owner. This project is not affiliated with, endorsed by, or supported by
> that manufacturer. Flashing custom firmware is done at your own risk and may void
> your warranty.

---

## ⚠️ Safety & legal

- **Mains voltage.** The controller switches AC outlets. Opening, wiring or modifying
  it exposes you to potentially lethal voltage. Only work on it unplugged and only if
  you are qualified to do so.
- **No warranty.** This software is provided "as is" (see [LICENSE](LICENSE), sections
  15–17). You are responsible for your plants, equipment and safety.
- **Grow responsibly.** Cultivating cannabis is regulated differently around the
  world. Know and follow the laws where you live.

---

## ✨ Features

- **VPD-first climate control** per chamber (up to 2 independent chambers) with
  hysteresis, day/night setpoints and configurable deadbands.
- **Grow-phase model** (Seedling, Clone, Veg, Bloom, Autoflower, Dry) with a
  **multi-step grow plan** that advances phases automatically by day count.
- **Photoperiod timers** with correct midnight wrap, sunrise/sunset **ramping** of the
  0–10 V lights, and a hard dark-period guarantee for bloom.
- **Light control** in % or by **PPFD** target, with **DLI** (daily light integral)
  tracking per phase.
- **CO₂ regulation** (gated to the light period) with a configurable fresh-air baseline.
- **Exhaust / intake / humidifier / dehumidifier / heater / AC** actuator logic, incl.
  a time-based AC fallback when the exhaust can't pull the temperature down.
- **10 switchable AC outlets** with per-outlet day/cycle schedules and chamber/role
  assignment, plus **2 dimmable 0–10 V** light channels.
- **Sensors** over RS-485 / RJ12 (temperature, humidity, CO₂, PPFD/PAR, substrate
  T/moisture/EC) and passive **BLE** climate sensors, all with calibration offsets.
- **Energy metering** (BL0940): power, energy, voltage, current.
- **Alarms**: over-temperature, mould risk, CO₂, sensor failure, light leak, water —
  with buzzer, MQTT and an on-device alarm log.
- **7-day history** + DLI saved to flash (survives reboots/OTA).
- **Home Assistant** MQTT auto-discovery — sensors and controls appear automatically.
- **Web dashboard + settings** served from the device. **Login** with username +
  password, plus an API token for automation.
- **Bilingual UI** — switch between **German and English** in Settings.
- **Safe OTA updates** with an automatic boot-validator / rollback (a bad image rolls
  back to the previous one without opening the case) — flash a new `.bin` straight from
  the Settings page or via curl.

---

## 🧰 Hardware

- **MCU:** ESP32-S3-WROOM-1 (N8R2 — 8 MB flash, 2 MB PSRAM).
- **Buses:** RS-485 (Modbus-RTU) for sensors/fans, 0–10 V analog for lights, BLE for
  wireless sensors, BL0940 for energy metering.
- The pin map lives in [`main/board_pins.h`](main/board_pins.h) and matches the
  Mars Hydro iHub-Pro main board. Other boards will need the pins adjusted.

---

## 🚀 Build & flash

You need [PlatformIO](https://platformio.org/) (the ESP-IDF toolchain is fetched
automatically on first build).

```bash
git clone https://github.com/thorstendjthb-glitch/ihub-pro-open.git
cd ihub-pro-open

pio run -e ihub                 # build
pio run -e ihub -t upload       # flash over USB (first time)
```

After the first flash you normally update **over the air** (see below) — no need to
open the case again.

> Nothing here contains credentials. Wi-Fi, MQTT and the login are all set at runtime
> (see *First boot*). If you want to pre-bake Wi-Fi into the binary for several
> devices, copy `main/secrets.h.example` to `main/secrets.h` (git-ignored).

---

## 🔌 First boot

1. On first start (no Wi-Fi configured) the device opens an **open setup hotspot**
   named **`iHub-Pro-Setup`**. Connect to it with your phone/laptop.
2. A **captive portal** pops up (or browse to `http://192.168.4.1/wifi`). Pick your
   network, enter the password, save — the device reboots and joins your Wi-Fi.
3. Find it on your network at **`http://ihub.local/`** (mDNS) or by its IP.
4. Open **Settings → Security & Login** and set a **username + password**. Done — the
   dashboard now requires a login.
5. (Optional) Switch the UI between **German and English** under
   **Settings → System & Network → Language**.

A second WPA2 hotspot named **`iHub`** also stays up in normal operation (password
shown in *Settings → System*) so you can always reach the dashboard at
`http://192.168.4.1/` even if your router blocks phone→device traffic.

---

## 🔐 Security model

The device is **open until you configure it**, so a fresh flash never locks you out.
There are two independent ways to authenticate:

| Method | For | How |
|---|---|---|
| **Login** (user + password) | humans, browser | `Settings → Security`; session cookie |
| **API token** | Home Assistant, scripts, headless OTA | `?key=<token>` or `Authorization: Bearer <token>` |

- Passwords are stored **salted + SHA-256 hashed** in NVS, never in plain text and
  never echoed back to the UI.
- Set a **password** for browser access and/or an **API token** for automation.
  Setting either one switches the device from "open" to "protected".
- Note: this is plain HTTP on your LAN (no TLS). Keep the device on a trusted/IoT
  network. NVS is not encrypted, so anyone with physical USB access can read stored
  secrets — treat physical access as full access.

---

## 🏠 Home Assistant

Set your broker under **Settings → MQTT** (`mqtt://homeassistant.local:1883` + user/
password). The device publishes **MQTT auto-discovery**, so sensors and controls show
up in Home Assistant automatically — no YAML. Leave MQTT blank to run fully standalone.

---

## ⬆️ OTA updates

```bash
# Easiest: Settings → Firmware update → pick firmware.bin → Upload & flash
#          (logged-in browser; shows a progress bar, then the device reboots)

# Or the minimal page: http://ihub.local/update  (append ?key=<token> if a token is set)

# Or curl (needs an API token set):
curl --data-binary @.pio/build/ihub/firmware.bin "http://ihub.local/ota?key=<TOKEN>"
```

A freshly flashed image is only marked valid after it runs stably for a minute;
otherwise the **boot-validator rolls back** to the previous firmware automatically.

---

## 📖 Documentation

The full manual — every screen, every setting, the climate logic and the recommended
grow values — is in **[docs/HANDBOOK.md](docs/HANDBOOK.md)**.

---

## ❤️ Support this project

This firmware is free and always will be. If it saved you money or a headache, you can
support continued development:

- ⭐ Star the repo and share it with other growers.
- ☕ Donate: **[Ko-fi](https://ko-fi.com/dj_thb)** · **[PayPal](https://www.paypal.me/djthb)**.
- 🛠️ **Want it ready to use?** Pre-flashed devices, the USB-flash mod and 3D-printed
  accessories may be available — see the repo for details.

Companies that need to ship this without the GPL's copyleft: a
**[commercial license](COMMERCIAL-LICENSE.md)** is available.

---

## 🤝 Contributing

PRs welcome — see [CONTRIBUTING.md](CONTRIBUTING.md). The UI ships in German and
English; **more languages** are an easy first contribution (just extend the dictionary
in `main/i18n_js.h`).

---

## 📜 License

GNU **GPL-3.0** — see [LICENSE](LICENSE). Copyright © the iHub-Pro Open contributors.
A separate commercial license is available (see above).
