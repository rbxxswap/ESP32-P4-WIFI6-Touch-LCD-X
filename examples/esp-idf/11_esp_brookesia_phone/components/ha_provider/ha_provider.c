/*
 * ha_provider - Implementation (Phase 2.2, HA-REST/Token).
 *
 * Pollt periodisch die HA-REST-API mit Bearer-Token:
 *   - Bresser-Einzelsensoren (bresser_prefix + Suffix) fuer aktuelle Werte
 *   - weather_entity fuer den Zustandstext (condition)
 */
#include "ha_provider.h"
#include "ha_config.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "ha_provider";

static SemaphoreHandle_t s_lock;
static ha_weather_t      s_weather;             /* geschuetzt durch s_lock */
static volatile bool     s_connected;
static volatile bool     s_started;

/* Config-Snapshot beim Start */
static char s_base[96];            /* "http://host:port" */
static char s_auth[300];           /* "Bearer <token>" */
static char s_bresser[64];         /* Bresser-Prefix */
static char s_weather_entity[64];
static char s_temp_entity[64];     /* separate Temperatur-Quelle (optional) */

static void lock(void)   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

/* GET /api/states/<entity> -> "state" nach out. true wenn ok und nicht unknown/unavailable. */
static bool fetch_state(const char *entity, char *out, size_t outsz)
{
    char url[288];
    snprintf(url, sizeof(url), "%s/api/states/%s", s_base, entity);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 6000,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return false;
    esp_http_client_set_header(cl, "Authorization", s_auth);

    bool ok = false;
    char *buf = malloc(4096);
    if (buf && esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        int r = esp_http_client_read_response(cl, buf, 4095);
        if (status == 200 && r > 0) {
            buf[r] = 0;
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *st = cJSON_GetObjectItem(root, "state");
                if (cJSON_IsString(st) && st->valuestring) {
                    snprintf(out, outsz, "%s", st->valuestring);
                    ok = strcmp(out, "unknown") != 0 && strcmp(out, "unavailable") != 0;
                }
                cJSON_Delete(root);
            }
        } else {
            ESP_LOGW(TAG, "HTTP %d fuer %s", status, entity);
        }
    }
    free(buf);
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return ok;
}

/* Numerischen Zustand von "<bresser_prefix>_<suffix>" holen. */
static bool fetch_bresser_float(const char *suffix, float *out)
{
    char entity[160];
    snprintf(entity, sizeof(entity), "%s_%s", s_bresser, suffix);
    char st[32];
    if (fetch_state(entity, st, sizeof(st))) {
        *out = strtof(st, NULL);
        return true;
    }
    return false;
}

static void poll_once(void)
{
    ha_weather_t w;
    lock(); w = s_weather; unlock();     /* auf bestehendem Stand aufsetzen */

    bool any = false;
    float f;

    if (s_weather_entity[0]) {
        char cond[24];
        if (fetch_state(s_weather_entity, cond, sizeof(cond))) {
            snprintf(w.condition, sizeof(w.condition), "%s", cond);
            any = true;
        }
    }
    /* Temperatur: separate Entity bevorzugt (z.B. sensor.aussentemperatur_min), sonst Bresser */
    if (s_temp_entity[0]) {
        char ts[32];
        if (fetch_state(s_temp_entity, ts, sizeof(ts))) { w.temperature = strtof(ts, NULL); w.has_temperature = true; any = true; }
    } else if (s_bresser[0]) {
        if (fetch_bresser_float("temperatur", &f)) { w.temperature = f; w.has_temperature = true; any = true; }
    }
    if (s_bresser[0]) {
        if (fetch_bresser_float("luftfeuchte",  &f)) { w.humidity    = f; w.has_humidity    = true; any = true; }
        if (fetch_bresser_float("wind",         &f)) { w.wind_speed  = f; w.has_wind        = true; any = true; }
        if (fetch_bresser_float("regenrate",    &f)) { w.rain_rate   = f; w.has_rain        = true; any = true; }
        if (fetch_bresser_float("uv_index",     &f)) { w.uv          = f; w.has_uv          = true; any = true; }
        if (fetch_bresser_float("beleuchtung",  &f)) { w.light_lx    = f; w.has_light       = true; any = true; }
        if (fetch_bresser_float("windrichtung", &f)) { w.wind_dir    = (int)f; w.has_dir    = true; any = true; }
    }

    if (any) {
        w.valid = true;
        w.revision++;
        lock(); s_weather = w; unlock();
        s_connected = true;
        ESP_LOGI(TAG, "Update: %.1fC %.0f%% wind %.1f rain %.1f uv %.1f lux %.0f cond=%s",
                 w.temperature, w.humidity, w.wind_speed, w.rain_rate, w.uv, w.light_lx, w.condition);
    } else {
        s_connected = false;
        ESP_LOGW(TAG, "Poll: keine Werte erhalten");
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));     /* Netif etwas Zeit geben */
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t ha_provider_start(void)
{
    if (s_started) return ESP_OK;

    const ha_config_t *cfg = ha_config_get();
    if (!cfg || cfg->ha_host[0] == '\0' || cfg->ha_token[0] == '\0') {
        ESP_LOGW(TAG, "Kein HA-Host/Token konfiguriert - ha_provider bleibt inaktiv");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t port = cfg->ha_port ? cfg->ha_port : 8123;
    snprintf(s_base, sizeof(s_base), "http://%s:%u", cfg->ha_host, (unsigned)port);
    snprintf(s_auth, sizeof(s_auth), "Bearer %s", cfg->ha_token);
    snprintf(s_bresser, sizeof(s_bresser), "%s", cfg->bresser_prefix);
    snprintf(s_weather_entity, sizeof(s_weather_entity), "%s", cfg->weather_entity);
    snprintf(s_temp_entity, sizeof(s_temp_entity), "%s", cfg->temp_entity);

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    memset(&s_weather, 0, sizeof(s_weather));

    s_started = true;
    if (xTaskCreate(poll_task, "ha_poll", 8192, NULL, 4, NULL) != pdPASS) {
        s_started = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ha_provider (REST) gestartet: base=%s bresser='%s' weather='%s'",
             s_base, s_bresser, s_weather_entity);
    return ESP_OK;
}

bool ha_provider_get_weather(ha_weather_t *out)
{
    if (!out) return false;
    lock();
    *out = s_weather;
    unlock();
    return out->valid;
}

bool ha_provider_is_connected(void)
{
    return s_connected;
}
