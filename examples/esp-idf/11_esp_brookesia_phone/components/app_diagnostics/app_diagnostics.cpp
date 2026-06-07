/*
 * Diagnostics-App - Implementation (Brookesia 0.5 / LVGL v9)
 * D2: System-Info-Panel (Timer 2s, read-only, GUI-thread-safe).
 *     FW, WiFi/IP, Uptime, Heap intern/PSRAM, Reset-Reason, Chip/Cores, IDF.
 *     Log-Terminal (D3) + aktive Tests (D4) folgen.
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppDiag"
#include "esp_lib_utils.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "app_diagnostics.hpp"
extern "C" {
#include "wifi_helper.h"
#include "ota_updater.h"
}

#define APP_NAME "Diagnostics"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(app_diagnostics_icon_112_112);

namespace esp_brookesia::apps {

AppDiagnostics *AppDiagnostics::_instance = nullptr;

AppDiagnostics *AppDiagnostics::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new AppDiagnostics(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

AppDiagnostics::AppDiagnostics(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &app_diagnostics_icon_112_112, true, use_status_bar, use_navigation_bar)
{
}

AppDiagnostics::~AppDiagnostics()
{
}

/* ---- read-only helpers ---- */

static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "unknown";
    }
}

static const char *chip_model_str(esp_chip_model_t m)
{
    switch (m) {
        case CHIP_ESP32:   return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        case CHIP_ESP32P4: return "ESP32-P4";
        default:           return "ESP chip";
    }
}

/* UI handles (Singleton-App, static ok) */
static lv_obj_t *s_l_fw     = nullptr;
static lv_obj_t *s_l_wifi   = nullptr;
static lv_obj_t *s_l_uptime = nullptr;
static lv_obj_t *s_l_heap   = nullptr;
static lv_obj_t *s_l_psram  = nullptr;
static lv_obj_t *s_l_reset  = nullptr;
static lv_obj_t *s_l_chip   = nullptr;
static lv_obj_t *s_l_idf    = nullptr;

static void refresh_info(void)
{
    char buf[96];

    if (s_l_fw) {
        snprintf(buf, sizeof(buf), "Firmware:  %s", ota_updater_get_current_version());
        lv_label_set_text(s_l_fw, buf);
    }
    if (s_l_wifi) {
        char ip[16] = {0};
        wifi_helper_get_ip(ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "WiFi:  %s  (%s)",
                 wifi_helper_is_connected() ? "connected" : "offline", ip);
        lv_label_set_text(s_l_wifi, buf);
    }
    if (s_l_uptime) {
        uint64_t s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
        snprintf(buf, sizeof(buf), "Uptime:  %lluh %llum %llus",
                 s / 3600ULL, (s % 3600ULL) / 60ULL, s % 60ULL);
        lv_label_set_text(s_l_uptime, buf);
    }
    if (s_l_heap) {
        size_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t t = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        snprintf(buf, sizeof(buf), "Heap (int):  %u / %u KB", (unsigned)(f / 1024), (unsigned)(t / 1024));
        lv_label_set_text(s_l_heap, buf);
    }
    if (s_l_psram) {
        size_t f = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t t = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        if (t > 0) {
            snprintf(buf, sizeof(buf), "PSRAM:  %u / %u KB", (unsigned)(f / 1024), (unsigned)(t / 1024));
        } else {
            snprintf(buf, sizeof(buf), "PSRAM:  n/a");
        }
        lv_label_set_text(s_l_psram, buf);
    }
    if (s_l_reset) {
        snprintf(buf, sizeof(buf), "Reset:  %s", reset_reason_str());
        lv_label_set_text(s_l_reset, buf);
    }
    if (s_l_chip) {
        esp_chip_info_t ci;
        esp_chip_info(&ci);
        snprintf(buf, sizeof(buf), "Chip:  %s, %d core(s)", chip_model_str(ci.model), ci.cores);
        lv_label_set_text(s_l_chip, buf);
    }
    if (s_l_idf) {
        snprintf(buf, sizeof(buf), "IDF:  %s", esp_get_idf_version());
        lv_label_set_text(s_l_idf, buf);
    }
}

static void refresh_timer_cb(lv_timer_t *t)
{
    refresh_info();
}

bool AppDiagnostics::run(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    auto mk = [&](int y) -> lv_obj_t * {
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, "");
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 50, y);
        return l;
    };

    int y = 150;
    const int step = 64;
    s_l_fw     = mk(y); y += step;
    s_l_wifi   = mk(y); y += step;
    s_l_uptime = mk(y); y += step;
    s_l_heap   = mk(y); y += step;
    s_l_psram  = mk(y); y += step;
    s_l_reset  = mk(y); y += step;
    s_l_chip   = mk(y); y += step;
    s_l_idf    = mk(y); y += step;

    refresh_info();
    lv_timer_create(refresh_timer_cb, 2000, nullptr);

    return true;
}

bool AppDiagnostics::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AppDiagnostics, APP_NAME, []()
{
    return std::shared_ptr<AppDiagnostics>(AppDiagnostics::requestInstance(), [](AppDiagnostics * p) {});
})

} // namespace esp_brookesia::apps
