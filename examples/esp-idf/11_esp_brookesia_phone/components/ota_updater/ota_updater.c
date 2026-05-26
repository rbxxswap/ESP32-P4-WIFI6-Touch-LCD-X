/*
 * OTA-Updater - Implementation
 *
 *  - GET /repos/<OWNER>/<REPO>/releases/latest
 *  - JSON parsen, tag_name extrahieren
 *  - Vergleiche mit esp_app_get_description()->version
 *  - Wenn neuer: assets[].name == OTA_ASSET_NAME -> browser_download_url
 *  - esp_https_ota auf diese URL -> reboot
 *
 * GitHub-Cert via x509_crt_bundle (DigiCert).
 */

#include "ota_updater.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "ota_updater";

#define OTA_GH_API_URL    "https://api.github.com/repos/" CONFIG_OTA_GH_OWNER "/" CONFIG_OTA_GH_REPO "/releases/latest"
#define OTA_ASSET_NAME    CONFIG_OTA_ASSET_NAME
#define OTA_HTTP_BUF_SIZE 4096

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
    ESP_LOGI(TAG, "Aktuelle Firmware-Version: %s", current);

    char tag[64] = {0};
    char url[256] = {0};
    if (fetch_latest_release(tag, sizeof(tag), url, sizeof(url)) != ESP_OK) {
        ESP_LOGI(TAG, "Kein OTA-Check moeglich (keine Release-Info)");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Latest Release-Tag: %s", tag);
    if (version_compare(tag, current) <= 0) {
        ESP_LOGI(TAG, "Bereits aktuell, kein Update noetig");
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
