# ARCHITECTURE & DEV-REFERENZ — ESP32-P4-WIFI6-Touch-LCD-X (Fork rbxxswap)

> Dichte Referenz fuer Claude/Contributors. ERST diese Datei lesen, dann gezielt Code.
> Keine Secrets hier (Repo ist public). IPs/Tokens NIE committen.
> Stand: 2026-06-07. Bei Struktur-Aenderungen MITPFLEGEN.

## Repo / Branches
- Default-Branch: `main` — haelt NUR den Web-Flasher (`docs/`) + Pages-Deploy (`.github/workflows/pages.yml`).
- Firmware-Dev-Branch: `feature/brookesia-phone-full` — der eigentliche Code + `.github/workflows/build.yml`.
- gh CLI: eingeloggt als `rbxxswap`. ACHTUNG: ohne `-R` nutzt gh das UPSTREAM (`waveshareteam`)! Immer `-R rbxxswap/ESP32-P4-WIFI6-Touch-LCD-X`.

## Build & Release (build.yml, nur auf feature-Branch)
- Trigger: push auf main/feature/**, PR, `v*`-Tag, workflow_dispatch. Name des Workflows: "ESP-IDF Build".
- ESP-IDF v5.5.3 LTS. Target esp32p4. LVGL **v9** (siehe Icon-Format).
- Beispiel-Projekt: `examples/esp-idf/11_esp_brookesia_phone`.
- Release: NUR bei `v*`-Tag. `softprops/action-gh-release@v2` ERSTELLT das Release + haengt Assets an
  (`firmware-merged-esp32p4.bin`, bootloader.bin, partition-table.bin, flasher_args.json).
  `prerelease=true` automatisch, wenn der Tag ein `-` enthaelt (z.B. v0.1.0-alpha.1).
- Neue Version: Tag pushen. MIT `-` = Pre-Release (nur Advanced-Flasher). OHNE `-` = stabil (Hauptseite).

## Web-Flasher (auf main, siehe auch PROJECT-OVERVIEW.md)
- Live: https://rbxxswap.github.io/ESP32-P4-WIFI6-Touch-LCD-X/  (+ /advanced.html)
- Pages = GitHub Actions (`pages.yml`). Firmware wird serverseitig pro Release nach `fw/<tag>/` gestaged
  (same-origin, kein CORS; GitHub-Release-Assets haben KEIN Access-Control-Allow-Origin).
- esp-web-tools@10.2.1 (Manifest-Flash), esptool-js@0.6.0 (Custom-Binary; Datei als BINAER-STRING via readAsBinaryString).

## Tooling-Gotchas
- `git push` schreibt Status auf stderr -> in PowerShell als "Error"/CLIXML-Rauschen, kein echter Fehler.
  Erfolg an `<old>..<new> branch -> branch` erkennen.
- Grosse/mehrzeilige Datei-Inhalte: NICHT via PowerShell here-strings (timeout-anfaellig).
  Stattdessen Desktop Commander `write_file` gechunkt (~30 Zeilen, mode=rewrite dann append).
- UNC-Pfade (`\\downloader\...`): nur via PowerShell `[IO.File]::ReadAllText/WriteAllText`, nicht via Filesystem-Write-Tool.

## Firmware-Struktur (examples/esp-idf/11_esp_brookesia_phone)
```
main/main.cpp          app_main: bsp_display_start -> Phone-Objekt -> Stylesheet (800x1280) ->
                       phone->begin() -> initAppFromRegistry/installAppFromRegistry -> clock-timer ->
                       Background-Task: wifi_helper_start_blocking() + ota_updater_init_from_nvs()
main/CMakeLists.txt    REQUIRES wifi_helper ota_updater ; PRIV_REQUIRES brookesia_core <app-components>
                       --> NEUE APP HIER in PRIV_REQUIRES eintragen (sonst nicht gelinkt!)
components/
  esp32_p4_wifi6_touch_lcd_x/  BSP (in-tree). Step-4-BSP-Switch IST erledigt.
  brookesia_core/              Brookesia 0.5 (LVGL v9 UI-Framework)
  wifi_helper/                 C-API, siehe unten
  ota_updater/                 C-API, siehe unten
  app_settings/                VORLAGE fuer neue Apps (Aufbau 1:1 kopieren)
```

## App-Component-Anatomie (Muster = app_settings)
- Apps registrieren sich SELBST via Makro (kein Eintrag in main.cpp noetig, nur in main/CMakeLists PRIV_REQUIRES).
- CMakeLists.txt:
  ```
  file(GLOB_RECURSE PROJ_SRCS_C  ${PROJ_SRC}/*.c)
  file(GLOB_RECURSE PROJ_SRCS_CPP ${PROJ_SRC}/*.cpp)
  idf_component_register(SRCS ${PROJ_SRCS_C} ${PROJ_SRCS_CPP} INCLUDE_DIRS ${PROJ_SRC}
      REQUIRES brookesia_core wifi_helper ota_updater WHOLE_ARCHIVE)   # WHOLE_ARCHIVE = Selbst-Registrierung greift
  ```
- .hpp: `class AppX : public systems::phone::App` ; static `requestInstance(...)` Singleton ; override `run()`, `back()`.
- .cpp Kern:
  ```
  LV_IMG_DECLARE(appx_icon_112_112);                 // 126x126 ARGB8888 (Name historisch "112_112")
  AppX::AppX(...) : App("AppName", &appx_icon_112_112, true, use_status_bar, use_navigation_bar) {}
  bool AppX::run()  { lv_obj_t *scr = lv_screen_active(); /* Widgets auf scr, absolute Koords 800x1280 */ ... return true; }
  bool AppX::back() { ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "..."); return true; }
  ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AppX, APP_NAME, []() {
      return std::shared_ptr<AppX>(AppX::requestInstance(), [](AppX*){}); })
  ```

## Helper-APIs (C, extern "C")
- wifi_helper.h: `wifi_helper_start_blocking()`, `wifi_helper_is_connected()`, `wifi_helper_get_ip(char*,size_t)`.
  Scan: siehe `app_settings/app_settings_wifi.h`.
- ota_updater.h: `ota_updater_get_current_version()`, `_get_interval_sec()`, `_set_interval_sec(int)`,
  `_set_enabled(bool)`, `_check_and_update()`, `_init_from_nvs()`.

## LVGL v9 — Idiome, die nachweislich kompilieren (aus app_settings)
- `lv_obj_t *scr = lv_screen_active();`
- `lv_label_create(scr)`, `lv_label_set_text(lbl, "...")`, `snprintf` in Buffer.
- `lv_btn_create(scr)` (v8-Name funktioniert hier), `lv_obj_set_size/align`, Label via `lv_label_create(btn)+lv_obj_center`.
- `lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED|LV_EVENT_VALUE_CHANGED, user_data)`,
  im cb: `lv_event_get_user_data(e)`, `lv_obj_has_state(sw, LV_STATE_CHECKED)`.
- `lv_slider_create`, `lv_slider_set_range/value(.., LV_ANIM_OFF)`.
- `lv_timer_create(cb, period_ms, user_data)` — Refresh-Pattern (z.B. 2000ms).
- Icon-Descriptor (LVGL v9!):
  ```
  const lv_image_dsc_t appx_icon_112_112 = {
    .header.cf = LV_COLOR_FORMAT_ARGB8888, .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 126, .header.h = 126, .data_size = 126*126*4, .data = appx_icon_112_112_map };
  ```
  Map = uint8_t[] in BGRA-Reihenfolge (B,G,R,A) pro Pixel. 126x126 generieren (Pillow -> C-Array).

## Threading / GUI-Regeln (WICHTIG)
- Brookesia-App-Code (run(), event-cbs) laeuft im LVGL/GUI-Thread.
- Lange/blockierende Ops (Netzwerk, OTA, Scan) -> in `xTaskCreate(...)`-Task auslagern.
- GUI aus Nicht-GUI-Thread NUR via `lv_async_call(fn, arg)` ODER mit `LvLockGuard gui_guard;` (esp_brookesia LvLock).
- Muster (app_settings): Button-cb startet Task; Task ruft `lv_async_call` mit Ergebnis-String auf Label.

## Pinout (verifiziert, Detail in PROJECT-OVERVIEW.md)
- GT911 Touch INT=GPIO33, RST=GPIO23 (0R-Bridges R32/R37 bestueckt, BSP-Header behauptet faelschlich NC).
- LCD_RST=GPIO27, LCD_BL_PWM=GPIO26 (LEDC). I2C SDA=GPIO7 SCL=GPIO8. Panel 800x1280 10.1".
- C6 (WiFi6 Co-Proc via SDIO/esp_hosted): EN=GPIO54, CLK=18 CMD=19 D0-D3=14-17.

## Aktueller Plan: Diagnose-App (app_diagnostics)
- D1 Geruest (Component + Icon + run()-Titel + back() + main/CMakeLists) — CI gruen, App im Launcher.
- D2 System-Info-Panel (Timer 2s: FW, WiFi/IP/RSSI, Uptime, Heap intern/PSRAM, Reset-Reason, Chip/Cores).
- D3 Log-Terminal (esp_log_set_vprintf -> Ringpuffer -> lv_textarea, Timer-Refresh, Clear-Btn).
- D4 Aktive Tests (WiFi-Rescan, Internet-HTTP-HEAD, NTP-Sync) je in Task + lv_async_call.
