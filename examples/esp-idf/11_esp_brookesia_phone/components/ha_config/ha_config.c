/*
 * ha_config - Implementation (Phase 2, Schritt P2.1).
 * NVS-Blob + lokale Web-Config-Page (esp_http_server).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_attr.h"
#include "ha_config.h"

#define TAG            "ha_config"
#define HA_NVS_NS      "ha_cfg"
#define HA_NVS_KEY     "cfg"

static EXT_RAM_BSS_ATTR ha_config_t s_cfg;
static bool          s_loaded;
static httpd_handle_t s_server;

/* ----------------------------------------------------------------------- */
/*  Defaults + NVS                                                         */
/* ----------------------------------------------------------------------- */

static void set_defaults(ha_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version = HA_CFG_VERSION;
    c->mqtt_port = 1883;
    c->ha_port = 8123;
    strncpy(c->base_topic, "ha_display", sizeof(c->base_topic) - 1);
}

/* NVS sicher initialisieren - ha_config_load() laeuft evtl. vor wifi_helper_init(). */
static void ensure_nvs(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

esp_err_t ha_config_load(void)
{
    set_defaults(&s_cfg);
    ensure_nvs();

    nvs_handle_t h;
    esp_err_t err = nvs_open(HA_NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        ha_config_t tmp;
        size_t len = sizeof(tmp);
        err = nvs_get_blob(h, HA_NVS_KEY, &tmp, &len);
        nvs_close(h);
        if (err == ESP_OK && len == sizeof(tmp) && tmp.version == HA_CFG_VERSION) {
            s_cfg = tmp;
            ESP_LOGI(TAG, "Config geladen (configured=%d, host=%s)",
                     s_cfg.configured, s_cfg.mqtt_host);
        } else {
            ESP_LOGW(TAG, "Kein/ungueltiges NVS-Blob -> Defaults");
        }
    } else {
        ESP_LOGW(TAG, "NVS-Namespace fehlt -> Defaults");
    }
    s_loaded = true;
    return ESP_OK;
}

const ha_config_t *ha_config_get(void)
{
    if (!s_loaded) ha_config_load();
    return &s_cfg;
}

esp_err_t ha_config_save(const ha_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    ensure_nvs();

    nvs_handle_t h;
    esp_err_t err = nvs_open(HA_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    s_cfg = *cfg;
    s_cfg.version = HA_CFG_VERSION;
    s_cfg.configured = 1;

    err = nvs_set_blob(h, HA_NVS_KEY, &s_cfg, sizeof(s_cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) ESP_LOGI(TAG, "Config gespeichert (host=%s)", s_cfg.mqtt_host);
    else               ESP_LOGE(TAG, "Config speichern fehlgeschlagen: %s", esp_err_to_name(err));
    return err;
}

bool ha_config_is_configured(void)
{
    return ha_config_get()->configured != 0;
}

/* ----------------------------------------------------------------------- */
/*  URL-Form-Parsing                                                       */
/* ----------------------------------------------------------------------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void urldecode(char *dst, const char *src, size_t dstsize)
{
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dstsize; i++) {
        char c = src[i];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && hexval(src[i + 1]) >= 0 && hexval(src[i + 2]) >= 0) {
            dst[di++] = (char)((hexval(src[i + 1]) << 4) | hexval(src[i + 2]));
            i += 2;
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = 0;
}

/* Sucht "key=value" im urlencoded body. Gibt true zurueck wenn gefunden. */
static bool form_get(const char *body, const char *key, char *out, size_t outsize)
{
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || p[-1] == '&') && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *e = strchr(v, '&');
            size_t vlen = e ? (size_t)(e - v) : strlen(v);
            char tmp[512];
            if (vlen >= sizeof(tmp)) vlen = sizeof(tmp) - 1;
            memcpy(tmp, v, vlen);
            tmp[vlen] = 0;
            urldecode(out, tmp, outsize);
            return true;
        }
        p += klen;
    }
    return false;
}

/* Setzt Feld nur, wenn im Formular vorhanden (Textfeld immer ueberschreiben). */
static void apply_text(const char *body, const char *key, char *dst, size_t dstsize)
{
    /* form_get schreibt urldecodiert + nullterminiert, begrenzt durch dstsize. */
    form_get(body, key, dst, dstsize);
}

/* Secrets nur ueberschreiben, wenn nicht leer (sonst alten Wert behalten). */
static void apply_secret(const char *body, const char *key, char *dst, size_t dstsize)
{
    char tmp[256] = {0};
    if (dstsize > sizeof(tmp)) dstsize = sizeof(tmp);
    if (form_get(body, key, tmp, dstsize) && tmp[0] != 0) {
        memcpy(dst, tmp, dstsize);
    }
}

/* ----------------------------------------------------------------------- */
/*  HTTP-Handler                                                           */
/* ----------------------------------------------------------------------- */

static const char *PAGE_HEAD =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>HA-Display Konfiguration</title><style>"
    "body{font-family:sans-serif;background:#0D1117;color:#fff;max-width:640px;margin:0 auto;padding:16px}"
    "h1{font-size:20px}h2{font-size:15px;color:#A0A7B4;margin-top:24px}"
    "label{display:block;margin:10px 0 4px;font-size:14px}"
    "input{width:100%;padding:8px;border-radius:8px;border:1px solid #2A2F37;background:#161B22;color:#fff;box-sizing:border-box}"
    "button{margin-top:20px;padding:12px 20px;border:0;border-radius:8px;background:#3BA4FF;color:#042C53;font-size:15px}"
    "small{color:#A0A7B4}</style></head><body>";

static esp_err_t get_handler(httpd_req_t *req)
{
    const ha_config_t *c = ha_config_get();
    char *buf = malloc(6144);
    if (!buf) return ESP_ERR_NO_MEM;

    int n = snprintf(buf, 6144,
        "%s<h1>HA-Display Konfiguration</h1>"
        "<form method=POST action=/save>"
        "<h2>MQTT (lesen)</h2>"
        "<label>Broker-Host</label><input name=mqtt_host value='%s'>"
        "<label>Port</label><input name=mqtt_port value='%u'>"
        "<label>Benutzer</label><input name=mqtt_user value='%s'>"
        "<label>Passwort <small>(leer = unveraendert)</small></label><input name=mqtt_pass type=password value=''>"
        "<label>base_topic</label><input name=base_topic value='%s'>"
        "<h2>Home Assistant (REST + steuern)</h2>"
        "<label>HA-Host</label><input name=ha_host value='%s'>"
        "<label>HA-Port</label><input name=ha_port value='%u'>"
        "<label>WebSocket-URL <small>(optional)</small></label><input name=ha_ws_url value='%s'>"
        "<label>Long-Lived Token <small>(leer = unveraendert)</small></label><input name=ha_token type=password value=''>"
        "<h2>Entities</h2>"
        "<label>Wetter/Forecast-Entity</label><input name=weather_entity value='%s'>"
        "<label>Bresser-Prefix <small>(aktuelle Werte)</small></label><input name=bresser_prefix value='%s'>"
        "<label>Temperatur-Entity <small>(optional, ersetzt Bresser-Temp)</small></label><input name=temp_entity value='%s'>"
        "<label>Energie <small>id|Label|Einheit;...</small></label><input name=energy_csv value='%s'>"
        "<label>Licht/Schalter <small>id|Label;...</small></label><input name=light_csv value='%s'>"
        "<label>Szenen <small>id|Label;...</small></label><input name=scene_csv value='%s'>"
        "<button type=submit>Speichern</button></form></body></html>",
        PAGE_HEAD, c->mqtt_host, (unsigned)c->mqtt_port, c->mqtt_user, c->base_topic,
        c->ha_host, (unsigned)c->ha_port, c->ha_ws_url, c->weather_entity, c->bresser_prefix,
        c->temp_entity, c->energy_csv, c->light_csv, c->scene_csv);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, (n > 0 && n < 6144) ? n : HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body zu gross/leer");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) return ESP_ERR_NO_MEM;

    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, body + received, total - received);
        if (r <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv fehlgeschlagen");
            return ESP_FAIL;
        }
        received += r;
    }
    body[total] = 0;

    /* Start vom aktuellen Cache, damit unveraenderte Secrets erhalten bleiben. */
    ha_config_t nc = *ha_config_get();

    apply_text(body, "mqtt_host", nc.mqtt_host, sizeof(nc.mqtt_host));
    char port[8];
    if (form_get(body, "mqtt_port", port, sizeof(port)) && port[0]) {
        int p = atoi(port);
        if (p > 0 && p < 65536) nc.mqtt_port = (uint16_t)p;
    }
    apply_text(body, "mqtt_user", nc.mqtt_user, sizeof(nc.mqtt_user));
    apply_secret(body, "mqtt_pass", nc.mqtt_pass, sizeof(nc.mqtt_pass));
    apply_text(body, "base_topic", nc.base_topic, sizeof(nc.base_topic));
    apply_text(body, "ha_host", nc.ha_host, sizeof(nc.ha_host));
    char haport[8];
    if (form_get(body, "ha_port", haport, sizeof(haport)) && haport[0]) {
        int p = atoi(haport);
        if (p > 0 && p < 65536) nc.ha_port = (uint16_t)p;
    }
    apply_text(body, "ha_ws_url", nc.ha_ws_url, sizeof(nc.ha_ws_url));
    apply_secret(body, "ha_token", nc.ha_token, sizeof(nc.ha_token));
    apply_text(body, "weather_entity", nc.weather_entity, sizeof(nc.weather_entity));
    apply_text(body, "bresser_prefix", nc.bresser_prefix, sizeof(nc.bresser_prefix));
    apply_text(body, "temp_entity", nc.temp_entity, sizeof(nc.temp_entity));
    apply_text(body, "energy_csv", nc.energy_csv, sizeof(nc.energy_csv));
    apply_text(body, "light_csv", nc.light_csv, sizeof(nc.light_csv));
    apply_text(body, "scene_csv", nc.scene_csv, sizeof(nc.scene_csv));

    free(body);

    esp_err_t err = ha_config_save(&nc);

    httpd_resp_set_type(req, "text/html");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req,
            "<!doctype html><meta charset=utf-8>"
            "<body style='font-family:sans-serif;background:#0D1117;color:#fff;padding:24px'>"
            "<h1>Gespeichert</h1><p>Konfiguration im NVS abgelegt. Neustart empfohlen.</p>"
            "<a style='color:#3BA4FF' href=/>Zurueck</a></body>");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS-Speichern fehlgeschlagen");
    }
    return ESP_OK;
}

esp_err_t ha_config_start_web(void)
{
    if (s_server) return ESP_OK;   /* idempotent */

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri_get  = { .uri = "/",     .method = HTTP_GET,  .handler = get_handler,  .user_ctx = NULL };
    httpd_uri_t uri_post = { .uri = "/save", .method = HTTP_POST, .handler = post_handler, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &uri_get);
    httpd_register_uri_handler(s_server, &uri_post);

    ESP_LOGI(TAG, "Web-Config-Page laeuft auf Port 80");
    return ESP_OK;
}
