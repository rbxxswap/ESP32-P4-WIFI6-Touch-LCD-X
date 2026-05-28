# ESP32-P4 Smart Display for Home Assistant

A custom firmware turning the **Waveshare ESP32-P4-WIFI6-Touch-LCD-X 10.1"** (1280×800 IPS, capacitive touch) into a wall-mounted **smart home / weather display** for [Home Assistant](https://www.home-assistant.io/).

Built on Espressif's [ESP-Brookesia](https://github.com/espressif/esp-brookesia) phone-style UI framework, with self-hosted **OTA updates via GitHub Releases** — flash once over USB, every update after that is wireless.

> **Status: early development (Phase 1).** The foundation (build pipeline, OTA, WiFi bring-up) is in place. UI apps are being built next. Expect breaking changes.

---

## What it will do

- **Weather display** — current conditions + forecast, pulled from Home Assistant
- **Home Assistant integration via two parallel providers:**
  - **MQTT** (statestream) for fast, low-latency entity updates
  - **WebSocket API** (long-lived access token) for service calls and richer queries
- **Touch dashboards** — control lights, scenes, climate, etc.
- **Kiosk mode** — display a live Home Assistant Lovelace view (streamed as image from a companion add-on)
- **Energy dashboard** — live PV / battery / grid data
- **Wireless OTA updates** straight from this repo's GitHub Releases

---

## Hardware

| | |
|---|---|
| Board | Waveshare ESP32-P4-WIFI6-Touch-LCD-X 10.1" |
| SoC | ESP32-P4 (dual-core RISC-V @ 360 MHz) + 32 MB PSRAM + 16 MB flash |
| WiFi/BT | ESP32-C6 co-processor over SDIO (`esp_hosted`) |
| Display | 1280×800 IPS, MIPI-DSI, JD9365 driver |
| Touch | GT911 capacitive (I²C) |

---

## Architecture

- **Base:** Espressif ESP-Brookesia phone demo (`examples/esp-idf/11_esp_brookesia_phone`)
- **ESP-IDF:** v5.5.3 LTS
- **Custom components:**
  - `ota_updater` — checks GitHub Releases, self-updates via `esp_https_ota`, configurable interval, NVS-persisted settings
  - `wifi_helper` — STA bring-up over the C6 co-processor, credentials in NVS
  - `app_settings` — on-device UI for WiFi setup and OTA control *(in progress)*
- **CI:** GitHub Actions builds firmware on every push and attaches binaries to Releases

---

## Flashing

### First time (USB)

Download `firmware-merged-esp32p4.bin` from the latest [Release](../../releases) (or from the **Actions** artifacts for dev builds). This single file contains bootloader + partition table + app at the correct offsets.

**Option A — browser (no tools):**
1. Open [ESP Launchpad](https://espressif.github.io/esp-launchpad/)
2. Connect the board via USB-C, select the serial port
3. Flash `firmware-merged-esp32p4.bin` at offset `0x0`

**Option B — esptool:**
```bash
pip install esptool
esptool.py --chip esp32p4 -p <PORT> write_flash 0x0 firmware-merged-esp32p4.bin
```
Replace `<PORT>` with your serial port (`COM5`, `/dev/ttyUSB0`, `/dev/tty.usbserial-*`).

### After first flash

No more USB needed. Configure WiFi on-device (Settings app), and the display pulls future updates automatically over OTA.

---

## Versioning & update cadence

Releases follow semantic versioning. The OTA default check interval depends on the build type (the on-device setting always overrides it):

| Version tag | Type | Default OTA interval |
|---|---|---|
| `v1.2.3-alpha.N` | alpha (prerelease) | 5 minutes |
| `v1.2.3-beta.N` | beta (prerelease) | 5 minutes |
| `v1.2.3` | stable release | 24 hours |

The interval is measured from each device's boot time, not a fixed wall-clock hour — this spreads update checks across devices and avoids a synchronized "update stampede".

---

## Building locally

Requires ESP-IDF v5.5.3 and the ESP32-P4 toolchain.
```bash
cd examples/esp-idf/11_esp_brookesia_phone
idf.py set-target esp32p4
idf.py build
```
CI builds the same target on every push — see [Actions](../../actions).

---

## Credits

- Hardware and base BSP by [Waveshare](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X) (this repo is a fork)
- UI framework: [ESP-Brookesia](https://github.com/espressif/esp-brookesia) by Espressif
- Built with [ESP-IDF](https://github.com/espressif/esp-idf)

## License

Firmware in this repository follows the licenses of its upstream components (Apache-2.0 for Espressif/Waveshare sources). Custom components are released under the same terms.
