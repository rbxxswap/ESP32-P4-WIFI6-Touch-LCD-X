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
#include <cstring>
extern "C" {
#include "ha_provider.h"
}

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

/* ---- Live-Daten aus ha_provider ---- */
static lv_obj_t   *s_lbl_temp  = nullptr;
static lv_obj_t   *s_lbl_cond  = nullptr;
static lv_obj_t   *s_lbl_humid = nullptr;
static lv_obj_t   *s_lbl_metric[4] = { nullptr, nullptr, nullptr, nullptr }; /* WIND, REGEN, UV, HELLIGKEIT */
static lv_timer_t *s_wx_timer  = nullptr;
static uint32_t    s_last_rev  = 0;

/* HA-weather-state -> deutscher Kurztext */
static const char *cond_to_de(const char *c)
{
    if (!c || !c[0]) return "--";
    if (!strcmp(c, "sunny") || !strcmp(c, "clear-night"))         return "Klar";
    if (!strcmp(c, "partlycloudy"))                               return "Teils bewoelkt";
    if (!strcmp(c, "cloudy"))                                     return "Bewoelkt";
    if (!strcmp(c, "fog"))                                        return "Nebel";
    if (!strcmp(c, "rainy"))                                      return "Regen";
    if (!strcmp(c, "pouring"))                                    return "Starkregen";
    if (!strcmp(c, "lightning") || !strcmp(c, "lightning-rainy")) return "Gewitter";
    if (!strcmp(c, "snowy") || !strcmp(c, "snowy-rainy"))         return "Schnee";
    if (!strcmp(c, "hail"))                                       return "Hagel";
    if (!strcmp(c, "windy") || !strcmp(c, "windy-variant"))       return "Windig";
    return c;   /* unbekannt: roh anzeigen */
}

/* Poll-Timer: uebernimmt neue Werte aus ha_provider in die Labels */
static void wx_update_cb(lv_timer_t *t)
{
    (void)t;
    ha_weather_t w;
    if (!ha_provider_get_weather(&w)) return;   /* noch keine Live-Daten */
    if (w.revision == s_last_rev)     return;   /* nichts Neues */
    s_last_rev = w.revision;

    char buf[32];
    if (w.has_temperature && s_lbl_temp) {
        snprintf(buf, sizeof(buf), "%.1f C", w.temperature);
        lv_label_set_text(s_lbl_temp, buf);
    }
    if (s_lbl_cond && w.condition[0]) {
        lv_label_set_text(s_lbl_cond, cond_to_de(w.condition));
    }
    if (w.has_humidity && s_lbl_humid) {
        snprintf(buf, sizeof(buf), "%.0f %%", w.humidity);
        lv_label_set_text(s_lbl_humid, buf);
    }
    if (w.has_wind && s_lbl_metric[0]) {
        snprintf(buf, sizeof(buf), "%.1f km/h", w.wind_speed);
        lv_label_set_text(s_lbl_metric[0], buf);
    }
    if (w.has_rain && s_lbl_metric[1]) {
        snprintf(buf, sizeof(buf), "%.1f mm/h", w.rain_rate);
        lv_label_set_text(s_lbl_metric[1], buf);
    }
    if (w.has_uv && s_lbl_metric[2]) {
        snprintf(buf, sizeof(buf), "%.1f", w.uv);
        lv_label_set_text(s_lbl_metric[2], buf);
    }
    if (w.has_light && s_lbl_metric[3]) {
        snprintf(buf, sizeof(buf), "%.0f klx", w.light_lx / 1000.0f);
        lv_label_set_text(s_lbl_metric[3], buf);
    }
}

/* Feuert beim Zerstoeren der App-Objekte -> Timer sicher entfernen (kein Dangling) */
static void wx_cleanup_cb(lv_event_t *e)
{
    (void)e;
    if (s_wx_timer) { lv_timer_delete(s_wx_timer); s_wx_timer = nullptr; }
    s_lbl_temp = s_lbl_cond = s_lbl_humid = nullptr;
    for (int i = 0; i < 4; i++) s_lbl_metric[i] = nullptr;
}

bool AppWeather::run(void)
{
    esp_rom_printf("WX_ENTER run()\n");

    /* Direkt auf den aktiven Screen bauen (wie app_settings / ARCHITECTURE.md),
     * KEIN deckendes Vollbild-Overlay -> das blockierte das Rendern. */
    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    esp_rom_printf("WX_ROOT done\n");

    lv_obj_t *top = mk_panel(root, 0, 0, 1256, 44);
    mk_label(top, "12:42   Sa, 25. Mai 2024", &lv_font_montserrat_16, COL_TXT, 0, 2);
    mk_label(top, "Hannover, DE", &lv_font_montserrat_16, COL_TXT2, 1040, 2);

    lv_obj_t *cur = mk_panel(root, 0, 66, 470, 300);
    mk_label(cur, "AKTUELL", &lv_font_montserrat_14, COL_RAIN, 0, 0);
    s_lbl_temp = mk_label(cur, "21.4 C", &lv_font_montserrat_44, COL_TXT, 0, 50);
    s_lbl_cond = mk_label(cur, "Meist sonnig", &lv_font_montserrat_24, COL_TXT, 0, 140);
    mk_label(cur, "Gefuehlt 24.0 C", &lv_font_montserrat_16, COL_TXT2, 0, 180);
    lv_obj_t *chip1 = mk_panel(cur, 0, 210, 210, 56);
    lv_obj_set_style_bg_color(chip1, lv_color_hex(COL_INNER), 0);
    s_lbl_humid = mk_label(chip1, "58 %", &lv_font_montserrat_22, COL_RAIN, 0, 0);
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
        s_lbl_metric[i] = mk_label(m, mv[i], &lv_font_montserrat_30, COL_TXT, 0, 50);
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
    /* Live-Update-Timer starten + Cleanup an Objekt-Lebensdauer koppeln */
    lv_obj_add_event_cb(cur, wx_cleanup_cb, LV_EVENT_DELETE, nullptr);
    s_last_rev = 0;
    s_wx_timer = lv_timer_create(wx_update_cb, 2000, nullptr);
    wx_update_cb(nullptr);   /* sofort erster Versuch, falls schon Daten da */

    esp_rom_printf("WX_DONE run() complete\n");
    return true;
}

bool AppWeather::back(void)
{
    if (s_wx_timer) { lv_timer_delete(s_wx_timer); s_wx_timer = nullptr; }
    s_lbl_temp = s_lbl_cond = s_lbl_humid = nullptr;
    for (int i = 0; i < 4; i++) s_lbl_metric[i] = nullptr;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AppWeather, APP_NAME, []()
{
    return std::shared_ptr<AppWeather>(AppWeather::requestInstance(), [](AppWeather * p) {});
})

} // namespace esp_brookesia::apps
