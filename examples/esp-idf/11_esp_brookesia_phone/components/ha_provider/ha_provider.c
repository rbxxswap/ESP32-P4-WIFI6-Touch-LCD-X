/*
 * ha_provider - Implementation (Phase 2.2, MQTT-Lesepfad).
 */
#include "ha_provider.h"
#include "ha_config.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "esp_log.h"

static const char *TAG = "ha_provider";

static esp_mqtt_client_handle_t s_client = NULL;
static SemaphoreHandle_t        s_lock   = NULL;
static volatile bool            s_connected = false;

static ha_weather_t s_weather;                 /* geschuetzt durch s_lock */
static char         s_sub_topic[48];           /* "<base>/#" */
static char         s_wx_prefix[128];          /* "<base>/weather/<object>/" (gross genug fuer Werror=format-truncation) */
static size_t       s_wx_prefix_len = 0;

/* ---- Hilfen ---- */

static void lock(void)   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

/* Kopiert bounded + null-terminiert (esp-mqtt liefert nicht null-terminiert). */
static void copy_bounded(char *dst, size_t dstsz, const char *src, int len)
{
    if (len < 0) len = 0;
    if ((size_t)len >= dstsz) len = (int)dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Verarbeitet ein weather-Attribut/State. field = z.B. "temperature". */
static void handle_weather_field(const char *field, const char *value)
{
    lock();
    if (strcmp(field, "state") == 0) {
        strncpy(s_weather.condition, value, sizeof(s_weather.condition) - 1);
        s_weather.condition[sizeof(s_weather.condition) - 1] = '\0';
        s_weather.valid = true;
        s_weather.revision++;
    } else if (strcmp(field, "temperature") == 0) {
        s_weather.temperature = strtof(value, NULL);
        s_weather.has_temperature = true;
        s_weather.valid = true;
        s_weather.revision++;
    } else if (strcmp(field, "humidity") == 0) {
        s_weather.humidity = strtof(value, NULL);
        s_weather.has_humidity = true;
        s_weather.valid = true;
        s_weather.revision++;
    } else if (strcmp(field, "wind_speed") == 0) {
        s_weather.wind_speed = strtof(value, NULL);
        s_weather.has_wind = true;
        s_weather.valid = true;
        s_weather.revision++;
    }
    unlock();
}

static void on_data(esp_mqtt_event_handle_t e)
{
    char topic[96];
    char value[48];
    copy_bounded(topic, sizeof(topic), e->topic, e->topic_len);
    copy_bounded(value, sizeof(value), e->data,  e->data_len);

    if (s_wx_prefix_len > 0 && strncmp(topic, s_wx_prefix, s_wx_prefix_len) == 0) {
        const char *field = topic + s_wx_prefix_len;
        handle_weather_field(field, value);
        ESP_LOGD(TAG, "weather %s = %s", field, value);
    }
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)args; (void)base;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT verbunden, subscribe '%s'", s_sub_topic);
        esp_mqtt_client_subscribe(s_client, s_sub_topic, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT getrennt");
        break;
    case MQTT_EVENT_DATA:
        on_data(e);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT-Fehler");
        break;
    default:
        break;
    }
}

/* ---- Public API ---- */

esp_err_t ha_provider_start(void)
{
    if (s_client) return ESP_OK;           /* schon gestartet (idempotent) */

    const ha_config_t *cfg = ha_config_get();
    if (!cfg || cfg->mqtt_host[0] == '\0') {
        ESP_LOGW(TAG, "Kein MQTT-Broker konfiguriert - ha_provider bleibt inaktiv");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    memset(&s_weather, 0, sizeof(s_weather));

    const char *base = (cfg->base_topic[0] != '\0') ? cfg->base_topic : "ha_display";
    snprintf(s_sub_topic, sizeof(s_sub_topic), "%s/#", base);

    /* weather_entity "weather.home" -> object "home" -> Prefix "<base>/weather/home/" */
    if (cfg->weather_entity[0] != '\0') {
        const char *dot = strchr(cfg->weather_entity, '.');
        const char *obj = dot ? dot + 1 : cfg->weather_entity;
        snprintf(s_wx_prefix, sizeof(s_wx_prefix), "%s/weather/%s/", base, obj);
        s_wx_prefix_len = strlen(s_wx_prefix);
    } else {
        s_wx_prefix_len = 0;
        ESP_LOGW(TAG, "Keine weather_entity gesetzt - Wetter-Parsing inaktiv");
    }

    char uri[96];
    uint16_t port = cfg->mqtt_port ? cfg->mqtt_port : 1883;
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", cfg->mqtt_host, (unsigned)port);

    esp_mqtt_client_config_t mcfg = {0};
    mcfg.broker.address.uri = uri;
    if (cfg->mqtt_user[0] != '\0') {
        mcfg.credentials.username = cfg->mqtt_user;
    }
    if (cfg->mqtt_pass[0] != '\0') {
        mcfg.credentials.authentication.password = cfg->mqtt_pass;
    }

    s_client = esp_mqtt_client_init(&mcfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init fehlgeschlagen");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    ESP_LOGI(TAG, "ha_provider gestartet: broker=%s, weather-prefix='%s'",
             uri, s_wx_prefix_len ? s_wx_prefix : "(keins)");
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
