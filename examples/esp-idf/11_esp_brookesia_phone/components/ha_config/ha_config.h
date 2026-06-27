/*
 * ha_config - Konfiguration der Home-Assistant-Anbindung (Phase 2, Schritt P2.1).
 *
 * Haelt Broker-/HA-Zugangsdaten + Entity-Mapping. Persistenz in NVS (namespace
 * "ha_cfg", ein Blob). Eingabe ueber lokale Web-Config-Page (esp_http_server),
 * da ein HA-Long-Lived-Token zu lang fuer die Touch-Tastatur ist.
 *
 * WICHTIG: Repo ist public. Keine Secrets im Code/Default - alles kommt zur
 * Laufzeit ueber die Web-Page in den NVS.
 *
 * Entity-Mapping-Format (CSV, manuell, Stufe 1):
 *   energy/light/scene:  "entity_id|Label[|Einheit];entity_id|Label[|Einheit];..."
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HA_CFG_VERSION 1

typedef struct {
    uint8_t  version;
    /* MQTT (lesen) */
    char     mqtt_host[64];      /* IP/Hostname des Brokers */
    uint16_t mqtt_port;          /* default 1883 */
    char     mqtt_user[48];
    char     mqtt_pass[64];
    char     base_topic[32];     /* mqtt_statestream base_topic, default "ha_display" */
    /* HA WebSocket (steuern + Forecast) */
    char     ha_ws_url[96];      /* z.B. ws://192.168.1.10:8123/api/websocket */
    char     ha_token[256];      /* Long-Lived Access Token */
    /* Entity-Mapping (CSV) */
    char     weather_entity[64]; /* z.B. weather.home */
    char     energy_csv[192];
    char     light_csv[192];
    char     scene_csv[192];
    uint8_t  configured;         /* 1 sobald einmal gespeichert */
} ha_config_t;

/** Laedt Config aus NVS in den In-Memory-Cache (Defaults wenn nicht vorhanden). */
esp_err_t ha_config_load(void);

/** Zeiger auf den In-Memory-Cache (read-only). Nie NULL nach ha_config_load(). */
const ha_config_t *ha_config_get(void);

/** Persistiert cfg in NVS und aktualisiert den Cache. */
esp_err_t ha_config_save(const ha_config_t *cfg);

/** true, wenn schon einmal eine gueltige Config gespeichert wurde. */
bool ha_config_is_configured(void);

/**
 * Startet die lokale Web-Config-Page (esp_http_server, Port 80).
 * Nach WiFi-Connect aufrufen. Idempotent.
 *   GET  /       -> Formular (Felder vorbefuellt, Secrets maskiert)
 *   POST /save   -> speichert in NVS
 */
esp_err_t ha_config_start_web(void);

#ifdef __cplusplus
}
#endif
