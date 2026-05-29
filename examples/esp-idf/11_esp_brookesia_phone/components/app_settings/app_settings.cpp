/*
 * Settings-App - Implementation (Brookesia 0.5)
 *
 * 5b-2 + 5b-4: OTA-Section interaktiv.
 *   - Auto-Update Switch (NVS-persistiert)
 *   - Slider fuer Intervall (5 min .. 24 h, Schrittweite 5 min)
 *   - "Check for updates now"-Button (in Background-Task)
 *   - Live-Refresh-Timer 2s
 * WiFi-Setup-UI (Scan-Liste + Keyboard) folgt in 5b-3.
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppSettings"
#include "esp_lib_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_settings.hpp"
#include "app_settings_wifi.h"
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

/* UI-Handles (Singleton-App, static ok) */
static lv_obj_t *s_fw_lbl          = nullptr;
static lv_obj_t *s_wifi_status_lbl = nullptr;
static lv_obj_t *s_ota_switch      = nullptr;
static lv_obj_t *s_ota_iv_lbl      = nullptr;
static lv_obj_t *s_ota_slider      = nullptr;
static lv_obj_t *s_ota_msg_lbl     = nullptr;

static void refresh_static_labels(void)
{
    char buf[80];
    if (s_fw_lbl) {
        snprintf(buf, sizeof(buf), "Firmware: %s", ota_updater_get_current_version());
        lv_label_set_text(s_fw_lbl, buf);
    }
    if (s_wifi_status_lbl) {
        char ip[16];
        wifi_helper_get_ip(ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "WiFi: %s (%s)",
                 wifi_helper_is_connected() ? "connected" : "offline", ip);
        lv_label_set_text(s_wifi_status_lbl, buf);
    }
    if (s_ota_iv_lbl) {
        int sec = ota_updater_get_interval_sec();
        if (sec >= 3600) snprintf(buf, sizeof(buf), "Interval: %d h", sec / 3600);
        else             snprintf(buf, sizeof(buf), "Interval: %d min", sec / 60);
        lv_label_set_text(s_ota_iv_lbl, buf);
    }
}

/* ---- Event-Callbacks ---- */

static void ota_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ota_updater_set_enabled(on);
    ESP_UTILS_LOGI("Auto-Update %s", on ? "ON" : "OFF");
}

/* Slider: 1..288 Steps mit je 5 min -> 5min..24h */
static void ota_slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = (lv_obj_t *)lv_event_get_user_data(e);
    int steps = lv_slider_get_value(sl);
    int sec   = steps * 300;
    ota_updater_set_interval_sec(sec);
    refresh_static_labels();
}

static void async_show_msg(void *arg)
{
    if (s_ota_msg_lbl) lv_label_set_text(s_ota_msg_lbl, (const char *)arg);
    refresh_static_labels();
}

static void manual_check_task(void *arg)
{
    lv_async_call(async_show_msg, (void *)"Checking...");
    ota_updater_check_and_update();
    /* Erfolg waere mit reboot rausgegangen; hier = bereits aktuell oder error */
    lv_async_call(async_show_msg, (void *)"Up to date.");
    vTaskDelete(NULL);
}

static void ota_check_btn_cb(lv_event_t *e)
{
    ESP_UTILS_LOGI("Manual OTA check requested");
    xTaskCreate(manual_check_task, "ota_manual", 8192, NULL, 5, NULL);
}

static void refresh_timer_cb(lv_timer_t *t)
{
    refresh_static_labels();
}

/* ---- UI-Aufbau ---- */

bool AppSettings::run(void)
{
    ESP_UTILS_LOGD("Run");
    lv_obj_t *root = lv_scr_act();
    lv_obj_t *tv = lv_tabview_create(root);
    lv_obj_t *tab_wifi = lv_tabview_add_tab(tv, "WiFi");
    lv_obj_t *scr = lv_tabview_add_tab(tv, "Updates");
    app_settings_wifi_section_create(tab_wifi);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Updates");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    s_fw_lbl = lv_label_create(scr);
    lv_obj_align(s_fw_lbl, LV_ALIGN_TOP_MID, 0, 80);

    s_wifi_status_lbl = lv_label_create(scr);
    lv_obj_align(s_wifi_status_lbl, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *ota_title = lv_label_create(scr);
    lv_label_set_text(ota_title, "--- OTA Updates ---");
    lv_obj_align(ota_title, LV_ALIGN_TOP_MID, 0, 200);

    lv_obj_t *sw_lbl = lv_label_create(scr);
    lv_label_set_text(sw_lbl, "Auto-Update");
    lv_obj_align(sw_lbl, LV_ALIGN_TOP_LEFT, 100, 250);

    s_ota_switch = lv_switch_create(scr);
    lv_obj_align(s_ota_switch, LV_ALIGN_TOP_LEFT, 300, 245);
    if (ota_updater_is_enabled()) lv_obj_add_state(s_ota_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_ota_switch, ota_switch_cb, LV_EVENT_VALUE_CHANGED, s_ota_switch);

    s_ota_iv_lbl = lv_label_create(scr);
    lv_obj_align(s_ota_iv_lbl, LV_ALIGN_TOP_LEFT, 100, 300);

    s_ota_slider = lv_slider_create(scr);
    lv_obj_set_width(s_ota_slider, 800);
    lv_obj_align(s_ota_slider, LV_ALIGN_TOP_LEFT, 100, 340);
    lv_slider_set_range(s_ota_slider, 1, 288);
    int cur_sec = ota_updater_get_interval_sec();
    lv_slider_set_value(s_ota_slider, cur_sec / 300, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ota_slider, ota_slider_cb, LV_EVENT_VALUE_CHANGED, s_ota_slider);

    lv_obj_t *check_btn = lv_btn_create(scr);
    lv_obj_set_size(check_btn, 300, 60);
    lv_obj_align(check_btn, LV_ALIGN_TOP_LEFT, 100, 400);
    lv_obj_add_event_cb(check_btn, ota_check_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(check_btn);
    lv_label_set_text(btn_lbl, "Check for updates now");
    lv_obj_center(btn_lbl);

    s_ota_msg_lbl = lv_label_create(scr);
    lv_label_set_text(s_ota_msg_lbl, "");
    lv_obj_align(s_ota_msg_lbl, LV_ALIGN_TOP_LEFT, 420, 415);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "(WiFi setup follows next)");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    refresh_static_labels();
    lv_timer_create(refresh_timer_cb, 2000, NULL);
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
