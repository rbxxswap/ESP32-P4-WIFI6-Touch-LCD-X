/*
 * Wetter-App - Implementation (Brookesia 0.5 / LVGL v9)
 * Scharfe Native-Icons (wxs/wxl), Bild-Kompass (wxc_0..7),
 * Reihen als LVGL-Flexbox (gleichmaessige, symmetrische Verteilung).
 * Live-Daten aus ha_provider (REST/Token).
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
#include <ctime>
#include <cmath>
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
LV_IMG_DECLARE(wxs_sun);  LV_IMG_DECLARE(wxl_sun);
LV_IMG_DECLARE(wxs_moon); LV_IMG_DECLARE(wxl_moon);
LV_IMG_DECLARE(wxs_partly); LV_IMG_DECLARE(wxl_partly);
LV_IMG_DECLARE(wxs_cloud); LV_IMG_DECLARE(wxl_cloud);
LV_IMG_DECLARE(wxs_rain); LV_IMG_DECLARE(wxl_rain);
LV_IMG_DECLARE(wxs_storm); LV_IMG_DECLARE(wxl_storm);
LV_IMG_DECLARE(wxc_0); LV_IMG_DECLARE(wxc_1); LV_IMG_DECLARE(wxc_2); LV_IMG_DECLARE(wxc_3);
LV_IMG_DECLARE(wxc_4); LV_IMG_DECLARE(wxc_5); LV_IMG_DECLARE(wxc_6); LV_IMG_DECLARE(wxc_7);

static const lv_image_dsc_t *CMP[8] = {
    &wxc_0, &wxc_1, &wxc_2, &wxc_3, &wxc_4, &wxc_5, &wxc_6, &wxc_7
};

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

/* Flex-Label: kein set_pos (Flex positioniert selbst) */
static lv_obj_t *mk_flabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

/* Icon in nativer Groesse (kein Skalieren) an fixer Position */
static lv_obj_t *mk_icon(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

/* Flex-Icon (fuer Zellen) */
static lv_obj_t *mk_ficon(lv_obj_t *parent, const lv_image_dsc_t *src)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    return img;
}

/* Flex-Row-Container: verteilt Kinder gleichmaessig (symmetrisch) */
static lv_obj_t *mk_flexrow(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

/* transparente Spalten-Zelle (stuendlich) */
static lv_obj_t *mk_cell(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, 5, 0);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return c;
}

/* Tages-Kachel (7-Tage) */
static lv_obj_t *mk_daycard(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_INNER), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return c;
}

/* ---- Live-Daten aus ha_provider ---- */
static lv_obj_t   *s_lbl_temp   = nullptr;
static lv_obj_t   *s_lbl_cond   = nullptr;
static lv_obj_t   *s_lbl_feels  = nullptr;
static lv_obj_t   *s_lbl_humid  = nullptr;
static lv_obj_t   *s_lbl_wdir   = nullptr;
static lv_obj_t   *s_img_compass = nullptr;
static lv_obj_t   *s_lbl_metric[4] = { nullptr, nullptr, nullptr, nullptr };
static lv_obj_t   *s_lbl_datetime = nullptr;
static lv_obj_t   *s_img_cond   = nullptr;
/* stuendliche Reihe */
static lv_obj_t   *s_hour_cell[8] = {0};
static lv_obj_t   *s_img_hour[8]  = {0};
static lv_obj_t   *s_lbl_hour[8]  = {0};
static lv_obj_t   *s_lbl_htemp[8] = {0};
static lv_obj_t   *s_lbl_hrain[8] = {0};
/* 7-Tage */
static lv_obj_t   *s_day_card[7]  = {0};
static lv_obj_t   *s_img_day[7]   = {0};
static lv_obj_t   *s_lbl_dwd[7]   = {0};
static lv_obj_t   *s_lbl_dhi[7]   = {0};
static lv_obj_t   *s_lbl_dlo[7]   = {0};
static lv_obj_t   *s_lbl_drain[7] = {0};
static uint32_t    s_fc_rev    = 0;
static lv_timer_t *s_wx_timer  = nullptr;
static uint32_t    s_last_rev  = 0;

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
    return c;
}

static const lv_image_dsc_t *icon_sm(const char *c)
{
    if (!c || !c[0])                                              return &wxs_cloud;
    if (!strcmp(c, "sunny"))                                      return &wxs_sun;
    if (!strcmp(c, "clear-night"))                                return &wxs_moon;
    if (!strcmp(c, "partlycloudy"))                               return &wxs_partly;
    if (!strcmp(c, "rainy") || !strcmp(c, "pouring"))             return &wxs_rain;
    if (!strcmp(c, "lightning") || !strcmp(c, "lightning-rainy")) return &wxs_storm;
    return &wxs_cloud;
}

static const lv_image_dsc_t *icon_lg(const char *c)
{
    if (!c || !c[0])                                              return &wxl_cloud;
    if (!strcmp(c, "sunny"))                                      return &wxl_sun;
    if (!strcmp(c, "clear-night"))                                return &wxl_moon;
    if (!strcmp(c, "partlycloudy"))                               return &wxl_partly;
    if (!strcmp(c, "rainy") || !strcmp(c, "pouring"))             return &wxl_rain;
    if (!strcmp(c, "lightning") || !strcmp(c, "lightning-rainy")) return &wxl_storm;
    return &wxl_cloud;
}

static float feels_like(const ha_weather_t &w)
{
    if (!w.has_temperature) return 0;
    float T  = w.temperature;
    float ws = w.has_wind ? w.wind_speed / 3.6f : 0.0f;
    float rh = w.has_humidity ? w.humidity : 50.0f;
    float e  = rh / 100.0f * 6.105f * expf(17.27f * T / (237.7f + T));
    return T + 0.33f * e - 0.70f * ws - 4.0f;
}

static int dir_idx(int deg)
{
    int i = ((deg + 22) / 45) % 8;
    if (i < 0) i += 8;
    return i;
}

static const char *dir8(int deg)
{
    static const char *d[8] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
    return d[dir_idx(deg)];
}

static void wx_update_cb(lv_timer_t *t)
{
    (void)t;
    if (s_lbl_datetime) {
        time_t now; struct tm tmv;
        time(&now); localtime_r(&now, &tmv);
        static const char *wd[7]  = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
        static const char *mo[12] = {"Januar", "Februar", "Maerz", "April", "Mai", "Juni",
                                     "Juli", "August", "September", "Oktober", "November", "Dezember"};
        char db[48];
        snprintf(db, sizeof(db), "%02d:%02d   %s, %d. %s %d",
                 tmv.tm_hour, tmv.tm_min, wd[tmv.tm_wday % 7], tmv.tm_mday,
                 mo[tmv.tm_mon % 12], tmv.tm_year + 1900);
        lv_label_set_text(s_lbl_datetime, db);
    }

    ha_forecast_t fc;
    if (ha_provider_get_forecast(&fc) && fc.revision != s_fc_rev) {
        s_fc_rev = fc.revision;
        char fb[16];
        static const char *dwd[7] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};

        for (int i = 0; i < 8; i++) {
            if (fc.hourly[i].used) {
                if (s_hour_cell[i]) lv_obj_remove_flag(s_hour_cell[i], LV_OBJ_FLAG_HIDDEN);
                if (s_lbl_hour[i])  { snprintf(fb, sizeof(fb), "%02d", fc.hourly[i].hour); lv_label_set_text(s_lbl_hour[i], fb); }
                if (s_img_hour[i])  lv_image_set_src(s_img_hour[i], icon_sm(fc.hourly[i].cond));
                if (s_lbl_htemp[i]) { snprintf(fb, sizeof(fb), "%.0f°", fc.hourly[i].temp); lv_label_set_text(s_lbl_htemp[i], fb); }
                if (s_lbl_hrain[i]) {
                    if (fc.hourly[i].rain_pct >= 0) { snprintf(fb, sizeof(fb), "%d%%", fc.hourly[i].rain_pct); lv_label_set_text(s_lbl_hrain[i], fb); }
                    else lv_label_set_text(s_lbl_hrain[i], " ");
                }
            } else if (s_hour_cell[i]) {
                lv_obj_add_flag(s_hour_cell[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        for (int i = 0; i < 7; i++) {
            if (fc.daily[i].used) {
                if (s_day_card[i]) lv_obj_remove_flag(s_day_card[i], LV_OBJ_FLAG_HIDDEN);
                if (s_lbl_dwd[i])   lv_label_set_text(s_lbl_dwd[i], dwd[fc.daily[i].wday % 7]);
                if (s_img_day[i])   lv_image_set_src(s_img_day[i], icon_sm(fc.daily[i].cond));
                if (s_lbl_dhi[i])   { snprintf(fb, sizeof(fb), "%.0f°", fc.daily[i].hi); lv_label_set_text(s_lbl_dhi[i], fb); }
                if (s_lbl_dlo[i])   { snprintf(fb, sizeof(fb), "%.0f°", fc.daily[i].lo); lv_label_set_text(s_lbl_dlo[i], fb); }
                if (s_lbl_drain[i]) {
                    if (fc.daily[i].rain_pct >= 0) { snprintf(fb, sizeof(fb), "%d%%", fc.daily[i].rain_pct); lv_label_set_text(s_lbl_drain[i], fb); }
                    else lv_label_set_text(s_lbl_drain[i], " ");
                }
            } else if (s_day_card[i]) {
                lv_obj_add_flag(s_day_card[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    ha_weather_t w;
    if (!ha_provider_get_weather(&w)) return;
    if (w.revision == s_last_rev)     return;
    s_last_rev = w.revision;

    char buf[32];
    if (w.has_temperature && s_lbl_temp) {
        snprintf(buf, sizeof(buf), "%.1f°", w.temperature);
        lv_label_set_text(s_lbl_temp, buf);
    }
    if (s_lbl_cond && w.condition[0]) lv_label_set_text(s_lbl_cond, cond_to_de(w.condition));
    if (s_img_cond && w.condition[0]) lv_image_set_src(s_img_cond, icon_lg(w.condition));
    if (s_lbl_feels && w.has_temperature) {
        snprintf(buf, sizeof(buf), "Gefuehlt %.0f°", feels_like(w));
        lv_label_set_text(s_lbl_feels, buf);
    }
    if (w.has_humidity && s_lbl_humid) {
        snprintf(buf, sizeof(buf), "%.0f %%", w.humidity);
        lv_label_set_text(s_lbl_humid, buf);
    }
    if (w.has_dir) {
        if (s_img_compass) lv_image_set_src(s_img_compass, CMP[dir_idx(w.wind_dir)]);
        if (s_lbl_wdir)    lv_label_set_text(s_lbl_wdir, dir8(w.wind_dir));
    }
    if (w.has_wind && s_lbl_metric[0]) { snprintf(buf, sizeof(buf), "%.1f km/h", w.wind_speed); lv_label_set_text(s_lbl_metric[0], buf); }
    if (w.has_rain && s_lbl_metric[1]) { snprintf(buf, sizeof(buf), "%.1f mm/h", w.rain_rate);  lv_label_set_text(s_lbl_metric[1], buf); }
    if (w.has_uv   && s_lbl_metric[2]) { snprintf(buf, sizeof(buf), "%.1f", w.uv);              lv_label_set_text(s_lbl_metric[2], buf); }
    if (w.has_light&& s_lbl_metric[3]) { snprintf(buf, sizeof(buf), "%.0f klx", w.light_lx/1000.0f); lv_label_set_text(s_lbl_metric[3], buf); }
}

static void wx_reset_handles(void)
{
    if (s_wx_timer) { lv_timer_delete(s_wx_timer); s_wx_timer = nullptr; }
    s_lbl_temp = s_lbl_cond = s_lbl_feels = s_lbl_humid = s_lbl_wdir = nullptr;
    s_img_compass = s_lbl_datetime = s_img_cond = nullptr;
    for (int i = 0; i < 4; i++) s_lbl_metric[i] = nullptr;
    for (int i = 0; i < 8; i++) { s_hour_cell[i] = s_img_hour[i] = s_lbl_hour[i] = s_lbl_htemp[i] = s_lbl_hrain[i] = nullptr; }
    for (int i = 0; i < 7; i++) {
        s_day_card[i] = s_img_day[i] = s_lbl_dwd[i] = nullptr;
        s_lbl_dhi[i] = s_lbl_dlo[i] = s_lbl_drain[i] = nullptr;
    }
}

static void wx_cleanup_cb(lv_event_t *e) { (void)e; wx_reset_handles(); }

bool AppWeather::run(void)
{
    esp_rom_printf("WX_ENTER run()\n");
    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* Kopfzeile */
    lv_obj_t *top = mk_panel(root, 0, 0, 1256, 44);
    s_lbl_datetime = mk_label(top, "--:--", &lv_font_montserrat_16, COL_TXT, 0, 2);
    mk_label(top, "Kiel, DE", &lv_font_montserrat_16, COL_TXT2, 1120, 2);

    /* AKTUELL */
    lv_obj_t *cur = mk_panel(root, 0, 66, 470, 300);
    mk_label(cur, "AKTUELL", &lv_font_montserrat_14, COL_RAIN, 0, 0);
    s_img_cond  = mk_icon(cur, &wxl_cloud, 306, 14);
    s_lbl_temp  = mk_label(cur, "--°", &lv_font_montserrat_44, COL_TXT, 0, 46);
    s_lbl_cond  = mk_label(cur, "--", &lv_font_montserrat_24, COL_TXT, 0, 136);
    s_lbl_feels = mk_label(cur, "Gefuehlt --°", &lv_font_montserrat_16, COL_TXT2, 0, 178);
    lv_obj_t *chip1 = mk_panel(cur, 0, 208, 214, 58);
    lv_obj_set_style_bg_color(chip1, lv_color_hex(COL_INNER), 0);
    s_lbl_humid = mk_label(chip1, "-- %", &lv_font_montserrat_22, COL_RAIN, 0, 0);
    mk_label(chip1, "Luftfeuchte", &lv_font_montserrat_12, COL_TXT2, 0, 30);
    s_img_compass = mk_icon(cur, CMP[0], 240, 184);
    mk_label(cur, "Windrichtung", &lv_font_montserrat_12, COL_TXT2, 334, 196);
    s_lbl_wdir = mk_label(cur, "--", &lv_font_montserrat_24, COL_AMBER, 334, 214);

    /* HEUTE - STUENDLICH */
    lv_obj_t *hr = mk_panel(root, 480, 66, 776, 300);
    mk_label(hr, "HEUTE - STUENDLICH", &lv_font_montserrat_14, COL_TXT2, 0, 0);
    lv_obj_t *hrow = mk_flexrow(hr, 0, 30, 752, 244);
    for (int i = 0; i < 8; i++) {
        lv_obj_t *cell = mk_cell(hrow, 82, 240);
        s_hour_cell[i] = cell;
        s_lbl_hour[i]  = mk_flabel(cell, "--", &lv_font_montserrat_16, COL_TXT2);
        s_img_hour[i]  = mk_ficon(cell, &wxs_cloud);
        s_lbl_htemp[i] = mk_flabel(cell, "--°", &lv_font_montserrat_22, COL_TXT);
        s_lbl_hrain[i] = mk_flabel(cell, " ", &lv_font_montserrat_14, COL_RAIN);
    }

    /* Metriken */
    const char *mt[4] = {"WIND", "REGEN", "UV-INDEX", "HELLIGKEIT"};
    int mx[4] = {0, 316, 632, 948};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *m = mk_panel(root, mx[i], 376, 306, 150);
        mk_label(m, mt[i], &lv_font_montserrat_14, COL_AMBER, 0, 0);
        s_lbl_metric[i] = mk_label(m, "--", &lv_font_montserrat_30, COL_TXT, 0, 50);
    }

    /* 7-TAGE-VORHERSAGE */
    lv_obj_t *wk = mk_panel(root, 0, 536, 1256, 250);
    mk_label(wk, "7-TAGE-VORHERSAGE", &lv_font_montserrat_14, COL_TXT2, 0, 0);
    lv_obj_t *wrow = mk_flexrow(wk, 0, 30, 1232, 190);
    for (int i = 0; i < 7; i++) {
        lv_obj_t *dc = mk_daycard(wrow, 164, 188);
        s_day_card[i]  = dc;
        s_lbl_dwd[i]   = mk_flabel(dc, "--", &lv_font_montserrat_20, COL_TXT);
        s_img_day[i]   = mk_ficon(dc, &wxs_cloud);
        s_lbl_dhi[i]   = mk_flabel(dc, "--°", &lv_font_montserrat_28, COL_TXT);
        s_lbl_dlo[i]   = mk_flabel(dc, "--°", &lv_font_montserrat_16, COL_TXT2);
        s_lbl_drain[i] = mk_flabel(dc, " ", &lv_font_montserrat_14, COL_RAIN);
    }

    lv_obj_add_event_cb(cur, wx_cleanup_cb, LV_EVENT_DELETE, nullptr);
    s_last_rev = 0;
    s_fc_rev = 0;
    s_wx_timer = lv_timer_create(wx_update_cb, 2000, nullptr);
    wx_update_cb(nullptr);

    esp_rom_printf("WX_DONE run() complete\n");
    return true;
}

bool AppWeather::back(void)
{
    wx_reset_handles();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AppWeather, APP_NAME, []()
{
    return std::shared_ptr<AppWeather>(AppWeather::requestInstance(), [](AppWeather * p) {});
})

} // namespace esp_brookesia::apps
