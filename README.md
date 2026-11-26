# DINO KEY V1 (ESP32-C3 + 1.3" SH1106 OLED)

![DINO KEY V1 Hardware](./Retro%20Black%20Gadget%20with%20OLED%20Display.webp)

Interactive wearable mini desk-companion with 9 modes, Bluetooth HID + Time/Battery sync, captive Wi-Fi NTP portal, playable Dino Run game, Stopwatch, 5-level brightness, and 20 pixel expressions.

## Modes
Single-click `GPIO 3` to cycle modes **1–8** (long-press toggles **Sleep**):
1. **HOME** — Stacked date & time, Bluetooth / `MAX17048` battery bar, animated blinking face (`2x Click`: next face)
2. **CLOCK** — Large digital time (`HH:MM:SS AM/PM`), sync source badge (`BLE`/`NTP`/`WEB`/`RTC`), and date
3. **STOPWATCH** — Live `MM:SS.d` timer (`2x Click`: start/pause, `Hold`: reset)
4. **BATTERY** — Bluetooth-synced (`0x180F` / `VIA BLE`) or `MAX17048` fuel gauge diagnostics
5. **DINO RUN** — Endless runner with cactus obstacles & NVS high score (`2x Click`: start, `Press`: jump)
6. **FULL FACES** — Full `128x64` expression viewer inside corner reticles (`2x Click`: cycle 20 faces)
7. **CONNECT** — Captive Wi-Fi AP (`DINO-KEY` / `192.168.4.1`) + saved 2.4 GHz NTP + Bluetooth (`DINO KEY`) sync status
8. **BRIGHT** — 5-step OLED contrast control saved in NVS (`2x Click`: cycle levels `1–5`)
9. **SLEEP** — Animated `Zzz` low-power screen (auto-triggers after 3 minutes of inactivity)

## Hardware Wiring (Seeed XIAO ESP32-C3 / ESP32-C3 SuperMini)
- **OLED `SDA` / `SCL`:** `GPIO 6` / `GPIO 7` (400 kHz I2C, `U8G2_R2` 180° flip)
- **Input (`GPIO 3`):** Auto-detects Active-HIGH TTP223 touch vs Active-LOW push button (`INPUT_PULLUP`)

## Sync & Project Files
- [RoboKey_AllInOne/RoboKey_AllInOne.ino](./RoboKey_AllInOne/RoboKey_AllInOne.ino) — Standalone single-file Arduino IDE firmware
- [CyberFace/CyberFace.ino](./CyberFace/CyberFace.ino) + [CyberFace/src](./CyberFace/src) — Modular Arduino IDE sketch
- [platformio.ini](./platformio.ini) + [src](./src) — PlatformIO project (`seeed_xiao_esp32c3`)
- [mac_sync](./mac_sync) + `DinoKeySync.app` — macOS menu-bar Bluetooth Time & Battery sync app
- [index.html](./index.html) — Browser 128×64 OLED simulator + Web Bluetooth / Web Serial sync tool
