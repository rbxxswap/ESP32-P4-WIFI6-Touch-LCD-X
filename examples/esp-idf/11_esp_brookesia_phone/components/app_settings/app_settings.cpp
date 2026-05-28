/*
 * Settings-App - Implementation (Brookesia 0.5)
 *
 * 5b-2: Anzeige-Skelett. Zeigt FW-Version, WiFi-Status, OTA-Settings als Labels.
 * Interaktive Elemente (WiFi-Scan, Keyboard, OTA-Toggle, Buttons) folgen in 5b-3/5b-4.
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppSettings"
#include "esp_lib_utils.h"
#include "app_settings.hpp"
extern "C" {
#include "wifi_helper.h"
#include "ota_updater.h"
}

#define APP_NAME "Settings"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(app_settings_icon_112_112);

namespace esp_brookesia::apps {

AppSettings *AppSettings::_instance = nullptr;

AppSettings *AppSettings::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new AppSettings(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

AppSettings::AppSettings(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &app_settings_icon_112_112, true, use_status_bar, use_navigation_bar)
{
}

AppSettings::~AppSettings()
{
}

bool AppSettings::run(void)
{
    ESP_UTILS_LOGD("Run");

    lv_obj_t *scr = lv_scr_act();
    char buf[80];

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    snprintf(buf, sizeof(buf), "Firmware: %s", ota_updater_get_current_version());
    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, buf);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 72);

    char ip[16];
    wifi_helper_get_ip(ip, sizeof(ip));
    snprintf(buf, sizeof(buf), "WiFi: %s (%s)",
             wifi_helper_is_connected() ? "connected" : "offline", ip);
    lv_obj_t *wifi = lv_label_create(scr);
    lv_label_set_text(wifi, buf);
    lv_obj_align(wifi, LV_ALIGN_TOP_MID, 0, 112);

    snprintf(buf, sizeof(buf), "Auto-Update: %s", ota_updater_is_enabled() ? "ON" : "OFF");
    lv_obj_t *ota = lv_label_create(scr);
    lv_label_set_text(ota, buf);
    lv_obj_align(ota, LV_ALIGN_TOP_MID, 0, 152);

    snprintf(buf, sizeof(buf), "Check interval: %d s", ota_updater_get_interval_sec());
    lv_obj_t *iv = lv_label_create(scr);
    lv_label_set_text(iv, buf);
    lv_obj_align(iv, LV_ALIGN_TOP_MID, 0, 192);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "(WiFi setup + controls coming next)");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 240);

    return true;
}

bool AppSettings::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AppSettings, APP_NAME, []()
{
    return std::shared_ptr<AppSettings>(AppSettings::requestInstance(), [](AppSettings * p) {});
})

} // namespace esp_brookesia::apps
