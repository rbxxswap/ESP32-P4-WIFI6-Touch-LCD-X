/*
 * ha_provider - Lesepfad Home Assistant -> Display (Phase 2.2).
 *
 * esp-mqtt-Client verbindet sich mit dem Broker (Zugangsdaten aus ha_config),
 * abonniert "<base_topic>/#" und parst die von HA-mqtt_statestream erzeugten
 * Topics in einen threadsafen Wetter-Cache. Die UI pollt den Cache ueber die
 * Revision (kein Callback noetig, LVGL-freundlich).
 *
 * Statestream-Topic-Schema (publish_attributes: true):
 *   <base_topic>/weather/<object>/state         -> Zustand (z.B. "sunny")
 *   <base_topic>/weather/<object>/temperature   -> Attribut (float)
 *   <base_topic>/weather/<object>/humidity      -> Attribut (float)
 *   <base_topic>/weather/<object>/wind_speed    -> Attribut (float)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;             /* true sobald mind. ein Wert empfangen wurde */
    char     condition[24];     /* weather-state, z.B. "sunny" */
    float    temperature;       /* Grad C */
    float    humidity;          /* % */
    float    wind_speed;        /* km/h (Einheit wie in HA) */
    bool     has_temperature;
    bool     has_humidity;
    bool     has_wind;
    uint32_t revision;          /* +1 bei jeder Aenderung -> UI erkennt Update */
} ha_weather_t;

/**
 * Startet den MQTT-Client anhand ha_config. Idempotent.
 * No-op (ESP_ERR_INVALID_STATE) wenn kein Broker konfiguriert ist.
 * ha_config_load() muss vorher gelaufen sein.
 */
esp_err_t ha_provider_start(void);

/** Kopiert den aktuellen Wetter-Snapshot threadsafe nach *out. Rueckgabe = out->valid. */
bool ha_provider_get_weather(ha_weather_t *out);

/** true, wenn der MQTT-Client aktuell mit dem Broker verbunden ist. */
bool ha_provider_is_connected(void);

#ifdef __cplusplus
}
#endif
