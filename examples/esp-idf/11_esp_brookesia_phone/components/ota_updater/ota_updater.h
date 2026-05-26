/*
 * OTA-Updater fuer ESP32-P4-Display.
 *
 * Konzept:
 *  - Checkt periodisch GitHub-Releases, laedt neuere FW via esp_https_ota, reboot.
 *  - Enable/Disable + Periode werden in NVS persistiert -> per UI steuerbar.
 *  - Periode wird ab Boot+Init gemessen (natuerlicher Jitter zwischen Geraeten,
 *    keine feste Tageszeit -> kein Update-Stampede).
 *
 * NVS-Layout: namespace "ota"
 *  - key "enabled"      (u8)  default 1
 *  - key "interval_sec" (i32) default 300 (5 min Phase-1-Test, spaeter 86400)
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/*  Init / Bootstrap                                                         */
/* ------------------------------------------------------------------------- */

/**
 * Liest enabled + interval_sec aus NVS, startet periodic-Check wenn enabled.
 * Muss NACH WiFi-Connect aufgerufen werden. Idempotent.
 */
esp_err_t ota_updater_init_from_nvs(void);

/* ------------------------------------------------------------------------- */
/*  Manueller One-Shot-Check                                                 */
/* ------------------------------------------------------------------------- */

/**
 * Sofortiger OTA-Check + ggf. Update (blocking).
 *  1. GET /repos/<owner>/<repo>/releases/latest
 *  2. Vergleiche tag_name mit esp_app_get_description()->version
 *  3. Wenn neuer: lade Asset via esp_https_ota -> reboot
 *
 * Bei erfolgreichem Update kehrt diese Funktion NICHT zurueck (Reboot).
 *
 * @return ESP_OK auch wenn kein Update verfuegbar, ESP_FAIL bei Fehler.
 */
esp_err_t ota_updater_check_and_update(void);

/* ------------------------------------------------------------------------- */
/*  Settings (NVS-persistiert)                                               */
/* ------------------------------------------------------------------------- */

/** Toggle, persistiert NVS, (re)startet oder stoppt periodic Timer. */
esp_err_t ota_updater_set_enabled(bool enabled);
bool      ota_updater_is_enabled(void);

/** Setzt Intervall (Sekunden), persistiert NVS, restartet Timer wenn enabled. */
esp_err_t ota_updater_set_interval_sec(int sec);
int       ota_updater_get_interval_sec(void);

/* ------------------------------------------------------------------------- */
/*  Status                                                                   */
/* ------------------------------------------------------------------------- */

/** Aktuelle Firmware-Version aus esp_app_get_description(). */
const char *ota_updater_get_current_version(void);

#ifdef __cplusplus
}
#endif
