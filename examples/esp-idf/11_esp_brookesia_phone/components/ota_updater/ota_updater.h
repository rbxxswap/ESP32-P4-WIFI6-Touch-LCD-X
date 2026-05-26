/*
 * OTA-Updater fuer ESP32-P4-Display
 * Checkt bei Start GitHub-Releases auf neuere Firmware,
 * laedt via esp_https_ota und reboot.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * OTA-Check + ggf. Update.
 *  1. GET /repos/<owner>/<repo>/releases/latest
 *  2. Vergleiche tag_name mit esp_app_get_description()->version
 *  3. Wenn neuer: lade Asset via esp_https_ota -> reboot
 *
 * Voraussetzungen: WiFi connected, ~30KB freier Heap.
 * Bei erfolgreichem Update kehrt diese Funktion NICHT zurueck (Reboot).
 *
 * @return ESP_OK auch wenn kein Update verfuegbar, ESP_FAIL bei Fehler.
 */
esp_err_t ota_updater_check_and_update(void);

/**
 * Aktuelle Firmware-Version als String (aus esp_app_get_description()).
 */
const char *ota_updater_get_current_version(void);

#ifdef __cplusplus
}
#endif
