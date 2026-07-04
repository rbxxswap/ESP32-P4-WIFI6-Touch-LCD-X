/*
 * ha_provider - Lesepfad Home Assistant -> Display (Phase 2.2, REST/Token).
 *
 * Pollt per HA-REST-API (Bearer-Token aus ha_config) im Hintergrund:
 *   - aktuelle Werte aus den Bresser-Einzelsensoren (bresser_prefix + Suffix)
 *   - Zustandstext (condition) aus der weather_entity
 * und legt sie threadsafe im Cache ab. Die UI pollt ueber die Revision.
 *
 * Kein MQTT/Statestream noetig - nur HA-Host + Token.
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
    char     condition[24];     /* weather-state der weather_entity, z.B. "cloudy" */
    float    temperature;       /* Grad C */
    float    humidity;          /* % */
    float    wind_speed;        /* km/h */
    float    rain_rate;         /* mm/h */
    float    uv;                /* UV-Index */
    float    light_lx;          /* Beleuchtung in Lux */
    int      wind_dir;          /* Grad */
    bool     has_temperature;
    bool     has_humidity;
    bool     has_wind;
    bool     has_rain;
    bool     has_uv;
    bool     has_light;
    bool     has_dir;
    uint32_t revision;          /* +1 bei jeder Aenderung -> UI erkennt Update */
} ha_weather_t;

/**
 * Startet den REST-Poll-Task anhand ha_config. Idempotent.
 * No-op (ESP_ERR_INVALID_STATE) wenn kein HA-Host/Token konfiguriert ist.
 * ha_config_load() muss vorher gelaufen sein.
 */
esp_err_t ha_provider_start(void);

/** Kopiert den aktuellen Wetter-Snapshot threadsafe nach *out. Rueckgabe = out->valid. */
bool ha_provider_get_weather(ha_weather_t *out);

/** true, wenn der letzte REST-Poll erfolgreich war. */
bool ha_provider_is_connected(void);

#ifdef __cplusplus
}
#endif
