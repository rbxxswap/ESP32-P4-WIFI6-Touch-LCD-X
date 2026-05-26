/*
 * WiFi Helper fuer ESP32-P4-Display.
 *
 * Phase-1-Minimal:
 *  - liest SSID/PW aus NVS-Namespace "wifi", Keys "ssid"/"pass"
 *  - Fallback auf CONFIG_WIFI_HELPER_DEFAULT_SSID/PASS aus sdkconfig
 *  - blockierender Connect mit Retry + Timeout
 *
 * Phase 2: Settings-App schreibt in NVS, Helper liest beim Boot.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Init NVS, esp_netif, esp_event, esp_wifi.
 * Idempotent - kann mehrfach aufgerufen werden.
 */
esp_err_t wifi_helper_init(void);

/**
 * Connect blocking. Liest Credentials aus NVS, fallback sdkconfig.
 * Wartet bis IP_EVENT_STA_GOT_IP oder Timeout.
 *
 * @return ESP_OK bei Verbindung, sonst ESP_FAIL/timeout error.
 */
esp_err_t wifi_helper_start_blocking(void);

/**
 * Speichert neue Credentials in NVS (fuer Settings-App).
 */
esp_err_t wifi_helper_store_credentials(const char *ssid, const char *pass);

bool wifi_helper_is_connected(void);

#ifdef __cplusplus
}
#endif
