/*
 * Wetter-App - Implementation (Brookesia 0.5 / LVGL v9)
 * MINIMAL-SAFE: nur Panels + Labels (wie app_settings), KEINE Bilder, KEIN
 * lv_chart, KEIN lv_arc, KEIN Grad-Zeichen. Ziel: zuverlaessig oeffnen.
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppWeather"
#include "esp_lib_utils.h"
#include "esp_rom_sys.h"
#include "app_weather.hpp"

#define APP_NAME "Wetter"

#define COL_BG     0x0D1117
#define COL_PANEL  0x161B22
#define COL_INNER  0x0D1117
#define COL_TXT    0xFFFFFF
#define COL_TXT2   0xA0A7B4
#define COL_AMBER  0xFFB020
#define COL_RAIN   0x3BA4FF

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(app_weather_icon_112_112);

namespace esp_brookesia::apps {

AppWeather *AppWeather::_instance = nullptr;

AppWeather *AppWeather::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new AppWeather(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

AppWeather::AppWeather(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &app_weather_icon_112_112, true, use_status_bar, use_navigation_bar)
{
}

AppWeather::~AppWeather()
{
}

static lv_obj_t *mk_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 14, 0);
    lv_obj_set_style_pad_all(p, 12, 0);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *font,
                          uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

bool AppWeather::run(void)
{
    esp_rom_printf("WX_ENTER run()\n");

    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1280, 800);
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    esp_rom_printf("WX_ROOT done\n");

    lv_obj_t *top = mk_panel(root, 0, 0, 1256, 44);
    mk_label(top, "12:42   Sa, 25. Mai 2024", &lv_font_montserrat_16, COL_TXT, 0, 2);
    mk_label(top, "Hannover, DE", &lv_font_montserrat_16, COL_TXT2, 1040, 2);

    lv_obj_t *cur = mk_panel(root, 0, 66, 470, 300);
    mk_label(cur, "AKTUELL", &lv_font_montserrat_14, COL_RAIN, 0, 0);
    mk_label(cur, "21.4 C", &lv_font_montserrat_44, COL_TXT, 0, 50);
    mk_label(cur, "Meist sonnig", &lv_font_montserrat_24, COL_TXT, 0, 140);
    mk_label(cur, "Gefuehlt 24.0 C", &lv_font_montserrat_16, COL_TXT2, 0, 180);
    lv_obj_t *chip1 = mk_panel(cur, 0, 210, 210, 56);
    lv_obj_set_style_bg_color(chip1, lv_color_hex(COL_INNER), 0);
    mk_label(chip1, "58 %", &lv_font_montserrat_22, COL_RAIN, 0, 0);
    mk_label(chip1, "Luftfeuchte", &lv_font_montserrat_12, COL_TXT2, 0, 28);
    esp_rom_printf("WX_AKTUELL done\n");

    lv_obj_t *hr = mk_panel(root, 480, 66, 776, 300);
    mk_label(hr, "HEUTE - STUENDLICH", &lv_font_montserrat_14, COL_TXT2, 0, 0);
    const char *hours[8] = {"13", "14", "15", "16", "17", "18", "19", "20"};
    const int   htemp[8] = {23, 24, 23, 21, 20, 19, 18, 17};
    for (int i = 0; i < 8; i++) {
        int x = i * 94;
        mk_label(hr, hours[i], &lv_font_montserrat_16, COL_TXT2, x + 10, 40);
        char t[8]; snprintf(t, sizeof(t), "%d", htemp[i]);
        mk_label(hr, t, &lv_font_montserrat_24, COL_TXT, x + 10, 90);
    }
    esp_rom_printf("WX_HOURLY done\n");

    const char *mt[4] = {"WIND", "REGEN", "UV-INDEX", "HELLIGKEIT"};
    const char *mv[4] = {"18 km/h", "0.8 mm/h", "7 Hoch", "72 klx"};
    int mx[4] = {0, 316, 632, 948};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *m = mk_panel(root, mx[i], 376, 306, 150);
        mk_label(m, mt[i], &lv_font_montserrat_14, COL_AMBER, 0, 0);
        mk_label(m, mv[i], &lv_font_montserrat_30, COL_TXT, 0, 50);
    }
    esp_rom_printf("WX_METRICS done\n");

    lv_obj_t *wk = mk_panel(root, 0, 536, 1256, 230);
    mk_label(wk, "7-TAGE-VORHERSAGE", &lv_font_montserrat_14, COL_TXT2, 0, 0);
    const char *wd[7]  = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
    const int   whi[7] = {24, 22, 19, 18, 25, 26, 21};
    const int   wlo[7] = {14, 15, 13, 12, 16, 17, 15};
    for (int i = 0; i < 7; i++) {
        int x = i * 180;
        lv_obj_t *dc = mk_panel(wk, x, 34, 168, 150);
        lv_obj_set_style_bg_color(dc, lv_color_hex(COL_INNER), 0);
        mk_label(dc, wd[i], &lv_font_montserrat_20, COL_TXT, 0, 0);
        char hi[8]; snprintf(hi, sizeof(hi), "%d", whi[i]);
        mk_label(dc, hi, &lv_font_montserrat_28, COL_TXT, 0, 50);
        char lo[8]; snprintf(lo, sizeof(lo), "%d", wlo[i]);
        mk_label(dc, lo, &lv_font_montserrat_18, COL_TXT2, 0, 95);
    }
    esp_rom_printf("WX_DONE run() complete\n");
    return true;
}

bool AppWeather::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AppWeather, APP_NAME, []()
{
    return std::shared_ptr<AppWeather>(AppWeather::requestInstance(), [](AppWeather * p) {});
})

} // namespace esp_brookesia::apps
