/*
 * Wetter-App - Implementation (Brookesia 0.5 / LVGL v9)
 *
 * Phase-2-Schritt 1: statische UI-Huelle nach Design-Vorlage.
 * Canvas-Design fuer Querformat 1280x800, Dark-Theme.
 * Datenanbindung (HA/MQTT/WS bzw. lokaler Sensor) folgt in spaeteren Schritten.
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppWeather"
#include "esp_lib_utils.h"
#include "app_weather.hpp"

#define APP_NAME "Wetter"
#define DEG "\xC2\xB0"   /* ° als UTF-8 */

/* Design-Tokens (aus der Vorlage) */
#define COL_BG     0x0D1117
#define COL_PANEL  0x161B22
#define COL_INNER  0x0D1117
#define COL_BORDER 0x2A2F37
#define COL_TXT    0xFFFFFF
#define COL_TXT2   0xA0A7B4
#define COL_AMBER  0xFFB020
#define COL_RAIN   0x3BA4FF
#define COL_GREEN  0x22C55E
#define COL_UV     0xFFC107

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(app_weather_icon_112_112);
LV_IMG_DECLARE(wicon_sun);
LV_IMG_DECLARE(wicon_partly);
LV_IMG_DECLARE(wicon_cloud);
LV_IMG_DECLARE(wicon_rain);
LV_IMG_DECLARE(wicon_storm);
LV_IMG_DECLARE(wicon_moon);

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

/* ---------- Helfer ---------- */

static lv_obj_t *mk_panel(lv_obj_t *parent, int x, int y, int w, int h, int pad)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_opa(p, LV_OPA_50, 0);
    lv_obj_set_style_radius(p, 14, 0);
    lv_obj_set_style_pad_all(p, pad, 0);
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

static lv_obj_t *mk_icon(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y, int scale256)
{
    lv_obj_t *im = lv_image_create(parent);
    lv_image_set_src(im, src);
    lv_obj_set_pos(im, x, y);
    if (scale256 != 256) lv_image_set_scale(im, scale256);
    return im;
}

static const lv_image_dsc_t *icon_for(char c)
{
    switch (c) {
    case 's': return &wicon_sun;
    case 'p': return &wicon_partly;
    case 'c': return &wicon_cloud;
    case 'r': return &wicon_rain;
    case 't': return &wicon_storm;
    case 'm': return &wicon_moon;
    default:  return &wicon_cloud;
    }
}

/* ---------- UI-Aufbau ---------- */

bool AppWeather::run(void)
{
    ESP_UTILS_LOGD("Run");

    /* Vollflaechiger Dark-Canvas */
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

    ESP_UTILS_LOGI("WX1 root ok");
    /* ----- Topbar ----- */
    lv_obj_t *top = mk_panel(root, 0, 0, 1256, 44, 8);
    lv_obj_t *clk = mk_label(top, "12:42", &lv_font_montserrat_22, COL_TXT, 0, 2);
    (void)clk;
    mk_label(top, "Sa, 25. Mai 2024", &lv_font_montserrat_14, COL_TXT2, 80, 8);
    mk_label(top, LV_SYMBOL_GPS " Hannover, DE", &lv_font_montserrat_16, COL_TXT2, 300, 6);
    mk_label(top, LV_SYMBOL_OK " Sensor OK", &lv_font_montserrat_16, COL_GREEN, 540, 6);
    mk_label(top, "Auf 05:24", &lv_font_montserrat_14, COL_AMBER, 900, 8);
    mk_label(top, "Unter 21:21", &lv_font_montserrat_14, COL_AMBER, 1010, 8);
    mk_label(top, LV_SYMBOL_WIFI, &lv_font_montserrat_16, COL_TXT2, 1150, 6);
    mk_label(top, LV_SYMBOL_SETTINGS, &lv_font_montserrat_16, COL_TXT2, 1200, 6);

    ESP_UTILS_LOGI("WX2 topbar ok");
    /* ----- AKTUELL (links) ----- */
    lv_obj_t *cur = mk_panel(root, 0, 66, 470, 300, 16);
    lv_obj_t *pill = lv_obj_create(cur);
    lv_obj_set_size(pill, 92, 26);
    lv_obj_set_pos(pill, 0, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(COL_RAIN), 0);
    lv_obj_set_style_radius(pill, 13, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *pl = mk_label(pill, "AKTUELL", &lv_font_montserrat_12, COL_RAIN, 0, 0);
    lv_obj_center(pl);

    mk_icon(cur, &app_weather_icon_112_112, 318, -6, 256);   /* grosse Sonne */
    mk_label(cur, "21.4", &lv_font_montserrat_44, COL_TXT, 0, 54);
    mk_label(cur, DEG "C", &lv_font_montserrat_24, COL_AMBER, 132, 58);
    mk_label(cur, "Meist sonnig", &lv_font_montserrat_20, COL_TXT, 0, 130);
    mk_label(cur, "Gefuehlt 24.0" DEG "C", &lv_font_montserrat_14, COL_TXT2, 0, 162);

    /* zwei Chips unten */
    lv_obj_t *chip1 = mk_panel(cur, 0, 200, 200, 56, 8);
    lv_obj_set_style_bg_color(chip1, lv_color_hex(COL_INNER), 0);
    lv_obj_set_style_border_width(chip1, 0, 0);
    mk_label(chip1, LV_SYMBOL_TINT " 58%", &lv_font_montserrat_20, COL_RAIN, 0, 0);
    mk_label(chip1, "Luftfeuchte", &lv_font_montserrat_12, COL_TXT2, 0, 28);
    lv_obj_t *chip2 = mk_panel(cur, 210, 200, 200, 56, 8);
    lv_obj_set_style_bg_color(chip2, lv_color_hex(COL_INNER), 0);
    lv_obj_set_style_border_width(chip2, 0, 0);
    mk_label(chip2, "24.0" DEG, &lv_font_montserrat_20, COL_AMBER, 0, 0);
    mk_label(chip2, "Gefuehlt", &lv_font_montserrat_12, COL_TXT2, 0, 28);

    ESP_UTILS_LOGI("WX3 aktuell ok");
    /* ----- HEUTE STUENDLICH (rechts) ----- */
    lv_obj_t *hr = mk_panel(root, 480, 66, 776, 300, 14);
    mk_label(hr, LV_SYMBOL_REFRESH " HEUTE - STUENDLICH", &lv_font_montserrat_14, COL_TXT2, 0, 0);

    const char *hours[8] = {"13", "14", "15", "16", "17", "18", "19", "20"};
    const char  hcond[8] = {'s', 's', 'c', 'r', 'r', 'r', 'c', 'm'};
    const int   htemp[8] = {23, 24, 23, 21, 20, 19, 18, 17};
    const int   hrain[8] = {10, 10, 20, 60, 70, 60, 30, 20};
    int colw = 92;
    for (int i = 0; i < 8; i++) {
        int x = i * colw;
        mk_label(hr, hours[i], &lv_font_montserrat_14, COL_TXT2, x + 22, 34);
        mk_icon(hr, icon_for(hcond[i]), x + 16, 54, 192);
        char t[8]; snprintf(t, sizeof(t), "%d" DEG, htemp[i]);
        mk_label(hr, t, &lv_font_montserrat_16, COL_TXT, x + 20, 116);
    }

    ESP_UTILS_LOGI("WX4 hourly cols ok, before chart");
    /* Temperaturkurve */
    lv_obj_t *chart = lv_chart_create(hr);
    lv_obj_set_size(chart, 740, 70);
    lv_obj_set_pos(chart, 0, 148);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_width(chart, 6, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 6, LV_PART_INDICATOR);
    lv_obj_set_style_radius(chart, 3, LV_PART_INDICATOR);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 8);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 14, 26);
    lv_chart_series_t *ser = lv_chart_add_series(chart, lv_color_hex(COL_AMBER), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 8; i++) lv_chart_set_next_value(chart, ser, htemp[i]);

    /* Regen-% */
    for (int i = 0; i < 8; i++) {
        char r[8]; snprintf(r, sizeof(r), "%d%%", hrain[i]);
        mk_label(hr, r, &lv_font_montserrat_14, COL_RAIN, i * colw + 18, 230);
    }

    ESP_UTILS_LOGI("WX5 chart+rain ok");
    /* ----- Metrik-Karten ----- */
    int my = 376, mh = 170, mw = 306;
    int mx[4] = {0, 316, 632, 948};

    /* WIND */
    lv_obj_t *wp = mk_panel(root, mx[0], my, mw, mh, 14);
    mk_label(wp, LV_SYMBOL_UP " WIND", &lv_font_montserrat_14, COL_TXT2, 0, 0);
    mk_label(wp, "18", &lv_font_montserrat_40, COL_TXT, 0, 36);
    mk_label(wp, "km/h", &lv_font_montserrat_16, COL_TXT2, 70, 56);
    mk_label(wp, "NW (315" DEG ")", &lv_font_montserrat_16, COL_TXT, 0, 92);
    mk_label(wp, "Boeen 42 km/h", &lv_font_montserrat_14, COL_TXT2, 0, 118);

    /* REGEN */
    lv_obj_t *rp = mk_panel(root, mx[1], my, mw, mh, 14);
    mk_label(rp, "REGEN", &lv_font_montserrat_14, COL_RAIN, 0, 0);
    mk_label(rp, "0.8", &lv_font_montserrat_40, COL_TXT, 0, 36);
    mk_label(rp, "mm/h", &lv_font_montserrat_16, COL_TXT2, 96, 56);
    mk_label(rp, "Heute 12.4 mm", &lv_font_montserrat_14, COL_TXT2, 0, 92);
    const int rbars[7] = {20, 35, 15, 60, 80, 70, 45};
    for (int i = 0; i < 7; i++) {
        lv_obj_t *b = lv_obj_create(rp);
        int bh = rbars[i] * 30 / 100;
        lv_obj_set_size(b, 24, bh);
        lv_obj_set_pos(b, i * 32, 120 + (30 - bh));
        lv_obj_set_style_bg_color(b, lv_color_hex(COL_RAIN), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }

    ESP_UTILS_LOGI("WX6 wind+rain cards ok, before arc");
    /* UV-INDEX (Arc-Gauge) */
    lv_obj_t *up = mk_panel(root, mx[2], my, mw, mh, 14);
    mk_label(up, "UV-INDEX", &lv_font_montserrat_14, COL_UV, 0, 0);
    lv_obj_t *arc = lv_arc_create(up);
    lv_obj_set_size(arc, 110, 110);
    lv_obj_set_pos(arc, 0, 28);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 11);
    lv_arc_set_value(arc, 7);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_INNER), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_UV), LV_PART_INDICATOR);
    mk_label(up, "7", &lv_font_montserrat_36, COL_UV, 140, 44);
    mk_label(up, "Hoch", &lv_font_montserrat_16, COL_AMBER, 140, 92);

    /* HELLIGKEIT */
    lv_obj_t *bp = mk_panel(root, mx[3], my, mw, mh, 14);
    mk_label(bp, "HELLIGKEIT", &lv_font_montserrat_14, COL_AMBER, 0, 0);
    mk_label(bp, "72", &lv_font_montserrat_40, COL_TXT, 0, 36);
    mk_label(bp, "klx", &lv_font_montserrat_16, COL_TXT2, 72, 56);
    mk_label(bp, "680", &lv_font_montserrat_24, COL_GREEN, 0, 100);
    mk_label(bp, "W/m" DEG " Solar", &lv_font_montserrat_14, COL_TXT2, 70, 108);

    ESP_UTILS_LOGI("WX7 metrics ok (arc done), before 7day");
    /* ----- 7-TAGE-VORHERSAGE ----- */
    lv_obj_t *wk = mk_panel(root, 0, 556, 1256, 220, 14);
    mk_label(wk, LV_SYMBOL_LIST " 7-TAGE-VORHERSAGE", &lv_font_montserrat_14, COL_TXT2, 0, 0);

    const char *wd[7]   = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
    const char *wdate[7] = {"26. Mai", "27. Mai", "28. Mai", "29. Mai", "30. Mai", "31. Mai", "01. Jun"};
    const char  wc[7]   = {'s', 'c', 'r', 't', 's', 's', 'r'};
    const int   whi[7]  = {24, 22, 19, 18, 25, 26, 21};
    const int   wlo[7]  = {14, 15, 13, 12, 16, 17, 15};
    const int   wrain[7] = {10, 20, 80, 70, 10, 10, 40};
    int cw = 172, gap = 8;
    for (int i = 0; i < 7; i++) {
        int x = 14 + i * (cw + gap);
        lv_obj_t *dc = mk_panel(wk, x - 14, 34, cw, 150, 10);
        lv_obj_set_style_bg_color(dc, lv_color_hex(COL_INNER), 0);
        lv_obj_set_style_border_width(dc, 0, 0);
        mk_label(dc, wd[i], &lv_font_montserrat_18, COL_TXT, 0, 0);
        mk_label(dc, wdate[i], &lv_font_montserrat_12, COL_TXT2, 0, 24);
        mk_icon(dc, icon_for(wc[i]), 88, 6, 192);
        char hilo[16]; snprintf(hilo, sizeof(hilo), "%d" DEG, whi[i]);
        mk_label(dc, hilo, &lv_font_montserrat_22, COL_TXT, 0, 70);
        char lo[8]; snprintf(lo, sizeof(lo), "%d" DEG, wlo[i]);
        mk_label(dc, lo, &lv_font_montserrat_16, COL_TXT2, 60, 76);
        char rr[8]; snprintf(rr, sizeof(rr), LV_SYMBOL_TINT " %d%%", wrain[i]);
        mk_label(dc, rr, &lv_font_montserrat_14, COL_RAIN, 0, 110);
    }

    ESP_UTILS_LOGI("WX8 run() complete (7day ok) - returning");
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
