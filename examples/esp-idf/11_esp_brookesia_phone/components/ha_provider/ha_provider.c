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
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "ha_provider";

static SemaphoreHandle_t s_lock;
static EXT_RAM_BSS_ATTR ha_weather_t  s_weather;   /* PSRAM, geschuetzt durch s_lock */
static EXT_RAM_BSS_ATTR ha_forecast_t s_fc;        /* PSRAM, geschuetzt durch s_lock */
static volatile bool     s_connected;
static volatile bool     s_started;

/* Config-Snapshot beim Start - im PSRAM, um internes BSS zu schonen (Boot-OOM vermeiden) */
static EXT_RAM_BSS_ATTR char s_base[96];            /* "http://host:port" */
static EXT_RAM_BSS_ATTR char s_auth[300];           /* "Bearer <token>" */
static EXT_RAM_BSS_ATTR char s_bresser[64];         /* Bresser-Prefix */
static EXT_RAM_BSS_ATTR char s_weather_entity[64];
static EXT_RAM_BSS_ATTR char s_temp_entity[64];     /* separate Temperatur-Quelle (optional) */

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

static time_t parse_iso_utc(const char *s);   /* Vorwaerts-Deklaration */

/* GET /api/states/<entity> -> attributes.<attr> (String) nach out. */
static bool fetch_attr(const char *entity, const char *attr, char *out, size_t outsz)
{
    char url[288];
    snprintf(url, sizeof(url), "%s/api/states/%s", s_base, entity);
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 6000, .method = HTTP_METHOD_GET };
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
                cJSON *at = cJSON_GetObjectItem(root, "attributes");
                cJSON *v  = at ? cJSON_GetObjectItem(at, attr) : NULL;
                if (cJSON_IsString(v) && v->valuestring) {
                    snprintf(out, outsz, "%s", v->valuestring);
                    ok = true;
                }
                cJSON_Delete(root);
            }
        }
    }
    free(buf);
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return ok;
}

/* ISO-Zeit (UTC) -> lokale "HH:MM" */
static void iso_to_local_hhmm(const char *iso, char *out, size_t outsz)
{
    time_t ep = parse_iso_utc(iso);
    if (ep == 0) { snprintf(out, outsz, "--:--"); return; }
    struct tm lt; localtime_r(&ep, &lt);
    snprintf(out, outsz, "%02d:%02d", lt.tm_hour, lt.tm_min);
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
        if (fetch_bresser_float("wind_boe",     &f)) { w.wind_gust   = f; w.has_gust        = true; any = true; }
        if (fetch_bresser_float("regenrate",    &f)) { w.rain_rate   = f; w.has_rain        = true; any = true; }
        if (fetch_bresser_float("uv_index",     &f)) { w.uv          = f; w.has_uv          = true; any = true; }
        if (fetch_bresser_float("beleuchtung",  &f)) { w.light_lx    = f; w.has_light       = true; any = true; }
        if (fetch_bresser_float("windrichtung", &f)) { w.wind_dir    = (int)f; w.has_dir    = true; any = true; }
        if (fetch_bresser_float("niederschlag", &f)) { w.precip_total = f;      any = true; }
        if (fetch_bresser_float("empfang_rssi", &f)) { w.rssi = (int)f;         any = true; }
    }

    /* Batterie: gleiches Geraet, aber binary_sensor.-Domain */
    if (s_bresser[0] && strncmp(s_bresser, "sensor.", 7) == 0) {
        char be[96], bs[16];
        snprintf(be, sizeof(be), "binary_sensor.%s_batterie_schwach", s_bresser + 7);
        if (fetch_state(be, bs, sizeof(bs))) { w.batt_low = (strcmp(bs, "on") == 0); any = true; }
    }

    /* Sonnenauf-/untergang aus sun.sun */
    {
        char iso[40];
        if (fetch_attr("sun.sun", "next_rising",  iso, sizeof(iso))) { iso_to_local_hhmm(iso, w.sunrise, sizeof(w.sunrise)); any = true; }
        if (fetch_attr("sun.sun", "next_setting", iso, sizeof(iso))) { iso_to_local_hhmm(iso, w.sunset,  sizeof(w.sunset));  any = true; }
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

/* Tage seit 1970-01-01 (Howard Hinnant days_from_civil) - Ersatz fuer fehlendes timegm */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

/* ISO-8601 (UTC, "2026-07-05T10:00:00+00:00") -> epoch */
static time_t parse_iso_utc(const char *s)
{
    if (!s) return 0;
    int Y, M, D, h, mi, se = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &se) >= 5) {
        long days = days_from_civil(Y, M, D);
        return (time_t)(days * 86400L + h * 3600L + mi * 60L + se);
    }
    return 0;
}

/* POST get_forecasts (type=daily|hourly), Antwort parsen -> fc fuellen. */
static bool fetch_forecast_type(const char *type, ha_forecast_t *fc, bool hourly)
{
    char url[200];
    snprintf(url, sizeof(url), "%s/api/services/weather/get_forecasts?return_response", s_base);
    char body[160];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"type\":\"%s\"}", s_weather_entity, type);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 9000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return false;
    esp_http_client_set_header(cl, "Authorization", s_auth);
    esp_http_client_set_header(cl, "Content-Type", "application/json");

    bool ok = false;
    char *buf = heap_caps_malloc(24576, MALLOC_CAP_SPIRAM);
    if (buf && esp_http_client_open(cl, strlen(body)) == ESP_OK) {
        esp_http_client_write(cl, body, strlen(body));
        esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        int r = esp_http_client_read_response(cl, buf, 24575);
        if (status == 200 && r > 0) {
            buf[r] = 0;
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *sr  = cJSON_GetObjectItem(root, "service_response");
                cJSON *ent = sr  ? cJSON_GetObjectItem(sr, s_weather_entity) : NULL;
                cJSON *arr = ent ? cJSON_GetObjectItem(ent, "forecast") : NULL;
                if (cJSON_IsArray(arr)) {
                    int idx = 0, maxn = hourly ? 8 : 7;
                    cJSON *it = NULL;
                    cJSON_ArrayForEach(it, arr) {
                        if (idx >= maxn) break;
                        cJSON *dt = cJSON_GetObjectItem(it, "datetime");
                        cJSON *tp = cJSON_GetObjectItem(it, "temperature");
                        cJSON *cd = cJSON_GetObjectItem(it, "condition");
                        cJSON *pp = cJSON_GetObjectItem(it, "precipitation_probability");
                        time_t ep = cJSON_IsString(dt) ? parse_iso_utc(dt->valuestring) : 0;
                        struct tm lt; localtime_r(&ep, &lt);
                        const char *cs = (cJSON_IsString(cd) && cd->valuestring) ? cd->valuestring : "";
                        int rp = cJSON_IsNumber(pp) ? (int)(pp->valuedouble + 0.5) : -1;
                        if (hourly) {
                            fc->hourly[idx].hour = lt.tm_hour;
                            fc->hourly[idx].temp = cJSON_IsNumber(tp) ? (float)tp->valuedouble : 0;
                            snprintf(fc->hourly[idx].cond, sizeof(fc->hourly[idx].cond), "%s", cs);
                            fc->hourly[idx].rain_pct = rp;
                            fc->hourly[idx].used = true;
                        } else {
                            cJSON *tl = cJSON_GetObjectItem(it, "templow");
                            fc->daily[idx].wday = lt.tm_wday;
                            fc->daily[idx].mday = lt.tm_mday;
                            fc->daily[idx].mon  = lt.tm_mon;
                            fc->daily[idx].hi = cJSON_IsNumber(tp) ? (float)tp->valuedouble : 0;
                            fc->daily[idx].lo = cJSON_IsNumber(tl) ? (float)tl->valuedouble : 0;
                            snprintf(fc->daily[idx].cond, sizeof(fc->daily[idx].cond), "%s", cs);
                            fc->daily[idx].rain_pct = rp;
                            fc->daily[idx].used = true;
                        }
                        idx++;
                    }
                    for (int k = idx; k < maxn; k++) {
                        if (hourly) fc->hourly[k].used = false; else fc->daily[k].used = false;
                    }
                    ok = idx > 0;
                }
                cJSON_Delete(root);
            }
        } else {
            ESP_LOGW(TAG, "forecast %s HTTP %d", type, status);
        }
    }
    free(buf);
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return ok;
}

static void poll_forecast(void)
{
    if (!s_weather_entity[0]) return;
    ha_forecast_t fc;
    lock(); fc = s_fc; unlock();
    bool changed = false;
    if (fetch_forecast_type("hourly", &fc, true))  changed = true;
    if (fetch_forecast_type("daily",  &fc, false)) changed = true;
    if (changed) {
        fc.valid = true;
        fc.revision++;
        lock(); s_fc = fc; unlock();
        ESP_LOGI(TAG, "Forecast aktualisiert (hourly[0]=%dh/%.0fC, daily[0]=%.0f/%.0f)",
                 fc.hourly[0].hour, fc.hourly[0].temp, fc.daily[0].hi, fc.daily[0].lo);
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));     /* Netif etwas Zeit geben */
    int cycle = 0;
    for (;;) {
        poll_once();
        if (cycle % 30 == 0) poll_forecast();   /* sofort + alle ~15 min (30 x 30s) */
        cycle++;
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
    memset(&s_fc, 0, sizeof(s_fc));

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

bool ha_provider_get_forecast(ha_forecast_t *out)
{
    if (!out) return false;
    lock();
    *out = s_fc;
    unlock();
    return out->valid;
}
