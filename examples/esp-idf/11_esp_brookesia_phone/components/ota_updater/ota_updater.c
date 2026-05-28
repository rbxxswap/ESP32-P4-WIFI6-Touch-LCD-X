/*
 * OTA-Updater - Implementation
 *
 *  - GET /repos/<OWNER>/<REPO>/releases/latest
 *  - JSON parsen, tag_name extrahieren
 *  - Vergleiche mit esp_app_get_description()->version
 *  - Wenn neuer: assets[].name == OTA_ASSET_NAME -> browser_download_url
 *  - esp_https_ota auf diese URL -> reboot
 *
 * Periodic-Check via esp_timer; Settings in NVS, von Settings-App steuerbar.
 * GitHub-Cert via x509_crt_bundle (DigiCert).
 */

#include "ota_updater.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "ota_updater";

#define OTA_GH_API_URL    "https://api.github.com/repos/" CONFIG_OTA_GH_OWNER "/" CONFIG_OTA_GH_REPO "/releases/latest"
#define OTA_ASSET_NAME    CONFIG_OTA_ASSET_NAME
#define OTA_HTTP_BUF_SIZE 4096

#define OTA_NVS_NS              "ota"
#define OTA_NVS_KEY_ENABLED     "enabled"
#define OTA_NVS_KEY_INTERVAL    "interval_sec"
#define OTA_DEFAULT_ENABLED     1
#define OTA_DEFAULT_INTERVAL    300   /* 5 min - Phase-1-Test, spaeter 86400 (24h) */

/* in-Memory Cache der Settings, vermeidet NVS-Reads bei jedem getter */
static bool             s_enabled       = false;
static int              s_interval_sec  = OTA_DEFAULT_INTERVAL;
static bool             s_initialised   = false;
static esp_timer_handle_t s_timer       = NULL;

/* Vergleich "X.Y.Z" / "vX.Y.Z". -1 a<b, 0 a==b, 1 a>b. */
static int version_compare(const char *a, const char *b)
{
    while (*a == 'v' || *a == 'V') a++;
    while (*b == 'v' || *b == 'V') b++;
    int va[3] = {0,0,0}, vb[3] = {0,0,0};
    sscanf(a, "%d.%d.%d", &va[0], &va[1], &va[2]);
    sscanf(b, "%d.%d.%d", &vb[0], &vb[1], &vb[2]);
    for (int i = 0; i < 3; i++) {
        if (va[i] < vb[i]) return -1;
        if (va[i] > vb[i]) return 1;
    }
    return 0;
}

typedef struct { char *buf; int buf_size; int data_len; } http_collect_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_collect_t *c = (http_collect_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && c && c->buf) {
        int copy = evt->data_len;
        if (c->data_len + copy >= c->buf_size) {
            copy = c->buf_size - c->data_len - 1;
        }
        if (copy > 0) {
            memcpy(c->buf + c->data_len, evt->data, copy);
            c->data_len += copy;
            c->buf[c->data_len] = 0;
        }
    }
    return ESP_OK;
}

/* Holt latest-release JSON, parst tag_name + Asset-URL. */
static esp_err_t fetch_latest_release(char *tag, size_t tag_size, char *url, size_t url_size)
{
    char *buf = malloc(OTA_HTTP_BUF_SIZE);
    if (!buf) return ESP_ERR_NO_MEM;

    http_collect_t collect = { .buf = buf, .buf_size = OTA_HTTP_BUF_SIZE, .data_len = 0 };

    esp_http_client_config_t cfg = {
        .url               = OTA_GH_API_URL,
        .event_handler     = http_event_handler,
        .user_data         = &collect,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(buf); return ESP_FAIL; }

    esp_http_client_set_header(client, "User-Agent", "esp32-p4-display-ota/1");
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GitHub-API request failed: err=%d status=%d", err, status);
        free(buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "JSON parse failed"); return ESP_FAIL; }

    esp_err_t ret = ESP_FAIL;
    cJSON *tag_node = cJSON_GetObjectItem(root, "tag_name");
    if (!cJSON_IsString(tag_node)) { ESP_LOGW(TAG, "no tag_name"); goto cleanup; }
    strncpy(tag, tag_node->valuestring, tag_size - 1);
    tag[tag_size - 1] = 0;

    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (!cJSON_IsArray(assets)) { ESP_LOGW(TAG, "no assets"); goto cleanup; }

    cJSON *asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        cJSON *dl   = cJSON_GetObjectItem(asset, "browser_download_url");
        if (cJSON_IsString(name) && cJSON_IsString(dl) &&
            strcmp(name->valuestring, OTA_ASSET_NAME) == 0) {
            strncpy(url, dl->valuestring, url_size - 1);
            url[url_size - 1] = 0;
            ret = ESP_OK;
            break;
        }
    }
    if (ret != ESP_OK) ESP_LOGW(TAG, "asset '%s' not in release", OTA_ASSET_NAME);

cleanup:
    cJSON_Delete(root);
    return ret;
}

/* ------------------------------------------------------------------------- */
/*  NVS-Settings                                                             */
/* ------------------------------------------------------------------------- */

/* Default-Intervall abhaengig vom Build-Typ (aus App-Version / git describe):
   - Version enthaelt "alpha"/"beta" oder einen "-" (= nicht exakt auf Release-Tag,
     also Prerelease oder Dev-Build) -> 5 min (schnelle Test-Updates)
   - sonst (sauberes Release-Tag wie "1.0.0") -> 24 h (sanft, gegen Update-Stampede)
   Der in NVS gespeicherte User-Wert ueberschreibt diesen Default immer. */
static int default_interval_from_version(void)
{
    const char *v = ota_updater_get_current_version();
    if (strstr(v, "alpha") || strstr(v, "beta") || strchr(v, '-')) {
        return 300;     /* prerelease / dev */
    }
    return 86400;       /* stable release */
}

static esp_err_t load_settings_nvs(void)
{
    int def_interval = default_interval_from_version();
    nvs_handle_t h;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* Namespace existiert noch nicht -> version-basierte Defaults */
        s_enabled      = OTA_DEFAULT_ENABLED;
        s_interval_sec = def_interval;
        ESP_LOGI(TAG, "Settings default: enabled=%d interval=%ds (v=%s)",
                 (int)s_enabled, s_interval_sec, ota_updater_get_current_version());
        return ESP_OK;
    }
    uint8_t en = OTA_DEFAULT_ENABLED;
    int32_t iv = 0;
    nvs_get_u8(h, OTA_NVS_KEY_ENABLED, &en);
    esp_err_t iv_err = nvs_get_i32(h, OTA_NVS_KEY_INTERVAL, &iv);
    nvs_close(h);
    s_enabled      = (en != 0);
    /* User-Wert nur nehmen wenn vorhanden + plausibel, sonst version-Default */
    s_interval_sec = (iv_err == ESP_OK && iv > 0) ? iv : def_interval;
    ESP_LOGI(TAG, "Settings: enabled=%d interval=%ds", (int)s_enabled, s_interval_sec);
    return ESP_OK;
}

static esp_err_t save_setting_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_u8(h, key, val);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t save_setting_i32(const char *key, int32_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_i32(h, key, val);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ------------------------------------------------------------------------- */
/*  Periodic-Timer                                                           */
/* ------------------------------------------------------------------------- */

/* Timer-Callback ruft check_and_update in einem eigenen Task auf, weil
   esp_timer-Callbacks NICHT blocken duerfen (sind im esp_timer-Service-Task). */
static void periodic_task(void *arg)
{
    ESP_LOGI(TAG, "Periodic OTA-Check (alle %d s)", s_interval_sec);
    ota_updater_check_and_update();
    vTaskDelete(NULL);
}

static void timer_cb(void *arg)
{
    if (!s_enabled) return;
    /* Task-Stack 8 KB, mDNS + TLS brauchen viel Stack */
    xTaskCreate(periodic_task, "ota_periodic", 8192, NULL, 5, NULL);
}

static esp_err_t start_periodic(void)
{
    if (s_timer == NULL) {
        esp_timer_create_args_t args = {
            .callback = timer_cb,
            .name     = "ota_periodic_timer",
        };
        esp_err_t err = esp_timer_create(&args, &s_timer);
        if (err != ESP_OK) return err;
    } else {
        /* Falls schon laeuft: erst stoppen */
        esp_timer_stop(s_timer);
    }
    uint64_t period_us = (uint64_t)s_interval_sec * 1000000ULL;
    ESP_LOGI(TAG, "Periodic-Timer start: %d s", s_interval_sec);
    return esp_timer_start_periodic(s_timer, period_us);
}

static esp_err_t stop_periodic(void)
{
    if (s_timer) {
        ESP_LOGI(TAG, "Periodic-Timer stop");
        return esp_timer_stop(s_timer);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/*  Public API                                                               */
/* ------------------------------------------------------------------------- */

const char *ota_updater_get_current_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->version : "?";
}

esp_err_t ota_updater_check_and_update(void)
{
    const char *current = ota_updater_get_current_version();
    ESP_LOGI(TAG, "Check: aktuelle Firmware-Version: %s", current);

    char tag[64] = {0};
    char url[256] = {0};
    if (fetch_latest_release(tag, sizeof(tag), url, sizeof(url)) != ESP_OK) {
        ESP_LOGI(TAG, "Kein Release-Info verfuegbar");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Latest Release-Tag: %s", tag);
    if (version_compare(tag, current) <= 0) {
        ESP_LOGI(TAG, "Bereits aktuell, kein Update");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Update verfuegbar -> %s, lade von %s", tag, url);

    esp_http_client_config_t http_cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 30000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Update erfolgreich, reboot...");
        esp_restart();
    }

    ESP_LOGE(TAG, "OTA-Update fehlgeschlagen: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ota_updater_init_from_nvs(void)
{
    load_settings_nvs();
    s_initialised = true;
    if (s_enabled) {
        return start_periodic();
    }
    ESP_LOGI(TAG, "OTA-Updater initialisiert (disabled, kein Timer)");
    return ESP_OK;
}

esp_err_t ota_updater_set_enabled(bool enabled)
{
    save_setting_u8(OTA_NVS_KEY_ENABLED, enabled ? 1 : 0);
    s_enabled = enabled;
    if (!s_initialised) return ESP_OK;
    return enabled ? start_periodic() : stop_periodic();
}

bool ota_updater_is_enabled(void)
{
    return s_enabled;
}

esp_err_t ota_updater_set_interval_sec(int sec)
{
    if (sec < 60) sec = 60;             /* min 1 min, Schutz vor Versehen */
    if (sec > 7*24*3600) sec = 7*24*3600;
    save_setting_i32(OTA_NVS_KEY_INTERVAL, sec);
    s_interval_sec = sec;
    if (s_initialised && s_enabled) {
        return start_periodic();        /* restart with new interval */
    }
    return ESP_OK;
}

int ota_updater_get_interval_sec(void)
{
    return s_interval_sec;
}
