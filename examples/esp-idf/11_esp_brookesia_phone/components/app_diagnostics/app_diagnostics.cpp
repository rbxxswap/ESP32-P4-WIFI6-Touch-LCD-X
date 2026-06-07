/*
 * Diagnostics-App - Implementation (Brookesia 0.5 / LVGL v9)
 * D1: Geruest. Zeigt Titel; System-Info/Log/Tests folgen in D2-D4.
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppDiag"
#include "esp_lib_utils.h"
#include "app_diagnostics.hpp"

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

bool AppDiagnostics::run(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "System info, logs and connectivity tests");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 110);

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
