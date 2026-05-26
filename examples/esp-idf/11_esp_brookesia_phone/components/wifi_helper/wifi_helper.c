/*
 * WiFi Helper - Implementation
 *
 * NVS-Layout:
 *   namespace "wifi"
 *     key "ssid" (str)
 *     key "pass" (str)
 *
 * Bei leerem NVS: Fallback auf CONFIG_WIFI_HELPER_DEFAULT_SSID/PASS.
 */

#include "wifi_helper.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_helper";

#define WIFI_BIT_CONNECTED  BIT0
#define WIFI_BIT_FAIL       BIT1
#define NVS_NS              "wifi"

static EventGroupHandle_t s_wifi_events = NULL;
static int                s_retry = 0;
static bool               s_initialised = false;
static bool               s_connected = false;
static esp_netif_t       *s_sta_netif = NULL;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry < CONFIG_WIFI_HELPER_MAX_RETRY) {
            s_retry++;
            ESP_LOGI(TAG, "Retry %d/%d", s_retry, CONFIG_WIFI_HELPER_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected, IP=" IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_BIT_CONNECTED);
    }
}

static esp_err_t load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    /* Erst NVS versuchen */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t ssid_len = ssid_size, pass_len = pass_size;
        esp_err_t r1 = nvs_get_str(h, "ssid", ssid, &ssid_len);
        esp_err_t r2 = nvs_get_str(h, "pass", pass, &pass_len);
        nvs_close(h);
        if (r1 == ESP_OK && r2 == ESP_OK && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Credentials aus NVS: SSID='%s'", ssid);
            return ESP_OK;
        }
    }
    /* Fallback sdkconfig */
    if (strlen(CONFIG_WIFI_HELPER_DEFAULT_SSID) > 0) {
        strncpy(ssid, CONFIG_WIFI_HELPER_DEFAULT_SSID, ssid_size - 1);
        strncpy(pass, CONFIG_WIFI_HELPER_DEFAULT_PASS, pass_size - 1);
        ssid[ssid_size - 1] = 0;
        pass[pass_size - 1] = 0;
        ESP_LOGI(TAG, "Credentials aus sdkconfig: SSID='%s'", ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Keine Credentials in NVS oder sdkconfig");
    return ESP_ERR_NOT_FOUND;
}

/* ------------------------------------------------------------------------- */
/*  Public API                                                               */
/* ------------------------------------------------------------------------- */

esp_err_t wifi_helper_init(void)
{
    if (s_initialised) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    s_initialised = true;
    return ESP_OK;
}

esp_err_t wifi_helper_start_blocking(void)
{
    if (!s_initialised) {
        esp_err_t e = wifi_helper_init();
        if (e != ESP_OK) return e;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    wifi_config_t wifi_cfg = {0};
    size_t slen = strlen(ssid);
    if (slen >= sizeof(wifi_cfg.sta.ssid)) slen = sizeof(wifi_cfg.sta.ssid) - 1;
    memcpy(wifi_cfg.sta.ssid, ssid, slen);
    size_t plen = strlen(pass);
    if (plen >= sizeof(wifi_cfg.sta.password)) plen = sizeof(wifi_cfg.sta.password) - 1;
    memcpy(wifi_cfg.sta.password, pass, plen);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retry = 0;
    xEventGroupClearBits(s_wifi_events, WIFI_BIT_CONNECTED | WIFI_BIT_FAIL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    TickType_t timeout = pdMS_TO_TICKS(CONFIG_WIFI_HELPER_CONNECT_TIMEOUT_SEC * 1000);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_BIT_CONNECTED | WIFI_BIT_FAIL,
                                           pdFALSE, pdFALSE, timeout);

    if (bits & WIFI_BIT_CONNECTED) {
        return ESP_OK;
    } else if (bits & WIFI_BIT_FAIL) {
        ESP_LOGE(TAG, "Connect fehlgeschlagen nach %d Retries", CONFIG_WIFI_HELPER_MAX_RETRY);
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "Connect-Timeout (%d s)", CONFIG_WIFI_HELPER_CONNECT_TIMEOUT_SEC);
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_helper_store_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !pass) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool wifi_helper_is_connected(void)
{
    return s_connected;
}
