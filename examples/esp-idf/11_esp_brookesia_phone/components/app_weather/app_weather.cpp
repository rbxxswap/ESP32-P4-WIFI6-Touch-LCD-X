/*
 * Wetter-App - Vollstaendiges Dashboard nach Vorlage (Brookesia 0.5 / LVGL v9).
 * Statusleiste (Sekunden, SENSOR-OK, Sonnenauf/-untergang), AKTUELL (Verlauf-BG,
 * 2 Chips), stuendliche Reihe mit Temperaturkurve, 4 Kacheln (Wind+Kompass,
 * Regen+Tagessumme+Balken, UV-Gauge, Helligkeit), 7-Tage mit Datum.
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
#define COL_GREEN  0x34D058
#define COL_RED    0xE0451F
#define COL_SKYTOP 0x1B3A5B
#define COL_SKYBOT 0x2F6B4A

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
LV_IMG_DECLARE(wxdrop);
LV_IMG_DECLARE(wxgauge);
LV_IMG_DECLARE(wxthermo);

static const lv_image_dsc_t *CMP[8] = { &wxc_0,&wxc_1,&wxc_2,&wxc_3,&wxc_4,&wxc_5,&wxc_6,&wxc_7 };

LV_FONT_DECLARE(seg_de_14); LV_FONT_DECLARE(seg_de_16); LV_FONT_DECLARE(seg_de_20);
LV_FONT_DECLARE(seg_de_22); LV_FONT_DECLARE(seg_de_24); LV_FONT_DECLARE(seg_de_26);
LV_FONT_DECLARE(seg_de_28); LV_FONT_DECLARE(seg_de_30); LV_FONT_DECLARE(seg_de_34);
LV_FONT_DECLARE(seg_de_36); LV_FONT_DECLARE(seg_de_48); LV_FONT_DECLARE(seg_de_72);

#define OX 12   /* horizontale Zentrierung (1256 auf 1280) */

namespace esp_brookesia::apps {

AppWeather *AppWeather::_instance = nullptr;

AppWeather *AppWeather::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new AppWeather(use_status_bar, use_navigation_bar);
    return _instance;
}

AppWeather::AppWeather(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &app_weather_icon_112_112, true, use_status_bar, use_navigation_bar) {}

AppWeather::~AppWeather() {}

static lv_obj_t *mk_panel(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 14, 0);
    lv_obj_set_style_pad_all(o, 12, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *mk_clabel(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c, int x, int y, int w)
{
    lv_obj_t *l = mk_label(p, t, f, c, x, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static lv_obj_t *mk_flabel(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static lv_obj_t *mk_icon(lv_obj_t *p, const lv_image_dsc_t *s, int x, int y)
{
    lv_obj_t *i = lv_image_create(p);
    lv_image_set_src(i, s); lv_obj_set_pos(i, x, y);
    return i;
}

/* skaliertes Icon (256=100%), Pivot links-oben */
static lv_obj_t *mk_icon_sc(lv_obj_t *p, const lv_image_dsc_t *s, int x, int y, int sc)
{
    lv_obj_t *i = lv_image_create(p);
    lv_image_set_src(i, s); lv_image_set_pivot(i, 0, 0);
    lv_image_set_scale(i, sc); lv_image_set_antialias(i, true);
    lv_obj_set_pos(i, x, y);
    return i;
}

static lv_obj_t *mk_ficon(lv_obj_t *p, const lv_image_dsc_t *s)
{
    lv_obj_t *i = lv_image_create(p);
    lv_image_set_src(i, s);
    return i;
}

static lv_obj_t *mk_flexrow(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_set_pos(r, x, y); lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

static lv_obj_t *mk_daycard(lv_obj_t *p, int w, int h)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_INNER), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_set_style_pad_row(c, 3, 0);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return c;
}

/* Tropfen + %-Label horizontal (fuer 7-Tage). Gibt %-Label zurueck. */
static lv_obj_t *mk_rain(lv_obj_t *p, const lv_font_t *f)
{
    lv_obj_t *row = lv_obj_create(p);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_image_set_src(lv_image_create(row), &wxdrop);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, " ");
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_RAIN), 0);
    return l;
}

/* kleiner Balken (Regenverlauf) */
static lv_obj_t *mk_bar(lv_obj_t *p, int x, int baseY, int w)
{
    lv_obj_t *b = lv_obj_create(p);
    lv_obj_set_size(b, w, 4);
    lv_obj_set_pos(b, x, baseY - 4);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_RAIN), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 2, 0);
    lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

/* ---- Statusleiste ---- */
static lv_obj_t *s_lbl_time=nullptr, *s_lbl_sec=nullptr, *s_lbl_date=nullptr;
static lv_obj_t *s_dot=nullptr, *s_lbl_sensor=nullptr;
static lv_obj_t *s_lbl_sunrise=nullptr, *s_lbl_sunset=nullptr;
/* ---- AKTUELL ---- */
static lv_obj_t *s_lbl_temp=nullptr, *s_img_cond=nullptr, *s_lbl_cond=nullptr;
static lv_obj_t *s_lbl_feels=nullptr, *s_lbl_humid=nullptr, *s_lbl_gfeels=nullptr;
/* ---- stuendlich (absolut) ---- */
static lv_obj_t *s_lbl_hour[8]={0}, *s_img_hour[8]={0}, *s_lbl_htemp[8]={0}, *s_lbl_hrain[8]={0};
static lv_obj_t *s_curve=nullptr;
static lv_point_precise_t s_cpts[8];
static int      s_hx[8];              /* Spalten-Mittelpunkte (Content-Koord.) */
/* ---- Kacheln ---- */
static lv_obj_t *s_img_compass=nullptr, *s_lbl_wdir=nullptr;
static lv_obj_t *s_lbl_metric[4]={nullptr,nullptr,nullptr,nullptr};  /* wind, regenrate, uv, licht */
static lv_obj_t *s_lbl_gust=nullptr, *s_lbl_precip=nullptr, *s_lbl_uvtxt=nullptr;
static lv_obj_t *s_uv_needle=nullptr; static lv_point_precise_t s_uvpts[2];
static int      s_uv_cx=0, s_uv_cy=0;
static lv_obj_t *s_bar[8]={0}; static int s_bar_base=0;
/* ---- 7-Tage ---- */
static lv_obj_t *s_day_card[7]={0}, *s_img_day[7]={0}, *s_lbl_dwd[7]={0};
static lv_obj_t *s_lbl_ddate[7]={0}, *s_lbl_dhi[7]={0}, *s_lbl_dlo[7]={0}, *s_lbl_drain[7]={0};

static uint32_t s_fc_rev=0, s_last_rev=0;
static lv_timer_t *s_wx_timer=nullptr;

static const char *MON[12]={"Januar","Februar","März","April","Mai","Juni","Juli","August","September","Oktober","November","Dezember"};
static const char *MONK[12]={"Jan","Feb","März","Apr","Mai","Jun","Jul","Aug","Sep","Okt","Nov","Dez"};

static const char *cond_to_de(const char *c)
{
    if (!c||!c[0]) return "--";
    if (!strcmp(c,"sunny")||!strcmp(c,"clear-night"))      return "Klar";
    if (!strcmp(c,"partlycloudy"))                         return "Teils bewölkt";
    if (!strcmp(c,"cloudy"))                               return "Bewölkt";
    if (!strcmp(c,"fog"))                                  return "Nebel";
    if (!strcmp(c,"rainy"))                                return "Regen";
    if (!strcmp(c,"pouring"))                              return "Starkregen";
    if (!strcmp(c,"lightning")||!strcmp(c,"lightning-rainy")) return "Gewitter";
    if (!strcmp(c,"snowy")||!strcmp(c,"snowy-rainy"))      return "Schnee";
    if (!strcmp(c,"hail"))                                 return "Hagel";
    if (!strcmp(c,"windy")||!strcmp(c,"windy-variant"))    return "Windig";
    return c;
}
static const lv_image_dsc_t *icon_sm(const char *c)
{
    if (!c||!c[0]) return &wxs_cloud;
    if (!strcmp(c,"sunny")) return &wxs_sun;
    if (!strcmp(c,"clear-night")) return &wxs_moon;
    if (!strcmp(c,"partlycloudy")) return &wxs_partly;
    if (!strcmp(c,"rainy")||!strcmp(c,"pouring")) return &wxs_rain;
    if (!strcmp(c,"lightning")||!strcmp(c,"lightning-rainy")) return &wxs_storm;
    return &wxs_cloud;
}
static const lv_image_dsc_t *icon_lg(const char *c)
{
    if (!c||!c[0]) return &wxl_cloud;
    if (!strcmp(c,"sunny")) return &wxl_sun;
    if (!strcmp(c,"clear-night")) return &wxl_moon;
    if (!strcmp(c,"partlycloudy")) return &wxl_partly;
    if (!strcmp(c,"rainy")||!strcmp(c,"pouring")) return &wxl_rain;
    if (!strcmp(c,"lightning")||!strcmp(c,"lightning-rainy")) return &wxl_storm;
    return &wxl_cloud;
}
static float feels_like(const ha_weather_t &w)
{
    if (!w.has_temperature) return 0;
    float T=w.temperature, ws=w.has_wind?w.wind_speed/3.6f:0.f, rh=w.has_humidity?w.humidity:50.f;
    float e=rh/100.f*6.105f*expf(17.27f*T/(237.7f+T));
    return T+0.33f*e-0.70f*ws-4.f;
}
static int dir_idx(int d){ int i=((d+22)/45)%8; if(i<0)i+=8; return i; }
static const char *dir8(int d){ static const char *x[8]={"N","NO","O","SO","S","SW","W","NW"}; return x[dir_idx(d)]; }

#define CURVE_TOP 150
#define CURVE_BOT 194

static void show(lv_obj_t *o, bool v)
{
    if (!o) return;
    if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_sensor_badge(bool connected, bool batt_low)
{
    if (!s_lbl_sensor || !s_dot) return;
    uint32_t col; const char *txt;
    if (!connected)      { col = COL_RED;   txt = "KEIN SENSOR"; }
    else if (batt_low)   { col = COL_AMBER; txt = "BATTERIE"; }
    else                 { col = COL_GREEN; txt = "SENSOR OK"; }
    lv_label_set_text(s_lbl_sensor, txt);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(col), 0);
}

static const char *uv_word(float uv)
{
    if (uv < 3)  return "Niedrig";
    if (uv < 6)  return "Mittel";
    if (uv < 8)  return "Hoch";
    if (uv < 11) return "Sehr hoch";
    return "Extrem";
}

static void wx_update_cb(lv_timer_t *t)
{
    (void)t;
    /* Uhr / Datum */
    if (s_lbl_time) {
        time_t now; struct tm tv; time(&now); localtime_r(&now, &tv);
        static const char *wd[7]={"So","Mo","Di","Mi","Do","Fr","Sa"};
        char b[24];
        snprintf(b, sizeof(b), "%02d:%02d", tv.tm_hour, tv.tm_min); lv_label_set_text(s_lbl_time, b);
        if (s_lbl_sec)  { snprintf(b, sizeof(b), ":%02d", tv.tm_sec); lv_label_set_text(s_lbl_sec, b); }
        if (s_lbl_date) { snprintf(b, sizeof(b), "%s, %d. %s %d", wd[tv.tm_wday%7], tv.tm_mday, MON[tv.tm_mon%12], tv.tm_year+1900); lv_label_set_text(s_lbl_date, b); }
    }

    /* Forecast */
    ha_forecast_t fc;
    if (ha_provider_get_forecast(&fc) && fc.revision != s_fc_rev) {
        s_fc_rev = fc.revision;
        char b[16];
        static const char *dwd[7]={"SO","MO","DI","MI","DO","FR","SA"};

        /* stuendlich + Kurve + Balken */
        float tmin=1e9f, tmax=-1e9f; int n=0;
        for (int i=0;i<8;i++) if (fc.hourly[i].used){ float tv=fc.hourly[i].temp; if(tv<tmin)tmin=tv; if(tv>tmax)tmax=tv; n++; }
        if (n==0){ tmin=0; tmax=1; }
        if (tmax<=tmin) tmax=tmin+1;
        for (int i=0;i<8;i++) {
            bool u = fc.hourly[i].used;
            if (u && s_lbl_hour[i])  { snprintf(b,sizeof(b),"%02d:00",fc.hourly[i].hour); lv_label_set_text(s_lbl_hour[i],b); }
            if (u && s_img_hour[i])  lv_image_set_src(s_img_hour[i], icon_sm(fc.hourly[i].cond));
            if (u && s_lbl_htemp[i]) { snprintf(b,sizeof(b),"%.0f°",fc.hourly[i].temp); lv_label_set_text(s_lbl_htemp[i],b); }
            if (u && s_lbl_hrain[i]) { if(fc.hourly[i].rain_pct>=0){snprintf(b,sizeof(b),"%d%%",fc.hourly[i].rain_pct);lv_label_set_text(s_lbl_hrain[i],b);} else lv_label_set_text(s_lbl_hrain[i]," "); }
            show(s_lbl_hour[i],u); show(s_img_hour[i],u); show(s_lbl_htemp[i],u); show(s_lbl_hrain[i],u);
            s_cpts[i].x = s_hx[i];
            s_cpts[i].y = CURVE_BOT - (int)((fc.hourly[i].temp - tmin)/(tmax-tmin)*(CURVE_BOT-CURVE_TOP));
            if (s_bar[i]) {
                int rp = u && fc.hourly[i].rain_pct>=0 ? fc.hourly[i].rain_pct : 0;
                int h = 3 + rp*25/100;
                lv_obj_set_height(s_bar[i], h);
                lv_obj_set_y(s_bar[i], s_bar_base - h);
            }
        }
        if (s_curve && n>=2) lv_line_set_points(s_curve, s_cpts, n);

        /* 7-Tage */
        for (int i=0;i<7;i++) {
            if (fc.daily[i].used) {
                if (s_day_card[i]) lv_obj_remove_flag(s_day_card[i], LV_OBJ_FLAG_HIDDEN);
                if (s_lbl_dwd[i])  lv_label_set_text(s_lbl_dwd[i], dwd[fc.daily[i].wday%7]);
                if (s_lbl_ddate[i]){ snprintf(b,sizeof(b),"%d. %s",fc.daily[i].mday,MONK[fc.daily[i].mon%12]); lv_label_set_text(s_lbl_ddate[i],b);}
                if (s_img_day[i])  lv_image_set_src(s_img_day[i], icon_sm(fc.daily[i].cond));
                if (s_lbl_dhi[i])  { snprintf(b,sizeof(b),"%.0f°",fc.daily[i].hi); lv_label_set_text(s_lbl_dhi[i],b);}
                if (s_lbl_dlo[i])  { snprintf(b,sizeof(b),"%.0f°",fc.daily[i].lo); lv_label_set_text(s_lbl_dlo[i],b);}
                if (s_lbl_drain[i]){ if(fc.daily[i].rain_pct>=0){snprintf(b,sizeof(b),"%d%%",fc.daily[i].rain_pct);lv_label_set_text(s_lbl_drain[i],b);} else lv_label_set_text(s_lbl_drain[i]," ");}
            } else if (s_day_card[i]) lv_obj_add_flag(s_day_card[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* aktuelle Werte */
    ha_weather_t w;
    bool have = ha_provider_get_weather(&w);
    set_sensor_badge(ha_provider_is_connected(), have && w.batt_low);
    if (!have) return;
    if (w.revision == s_last_rev) return;
    s_last_rev = w.revision;

    char buf[40];
    if (w.has_temperature && s_lbl_temp) { snprintf(buf,sizeof(buf),"%.1f°",w.temperature); lv_label_set_text(s_lbl_temp,buf); }
    if (s_lbl_cond && w.condition[0]) lv_label_set_text(s_lbl_cond, cond_to_de(w.condition));
    if (s_img_cond && w.condition[0]) lv_image_set_src(s_img_cond, icon_lg(w.condition));
    if (s_lbl_feels && w.has_temperature) { snprintf(buf,sizeof(buf),"Gefühlt %.0f°",feels_like(w)); lv_label_set_text(s_lbl_feels,buf); }
    if (s_lbl_gfeels && w.has_temperature){ snprintf(buf,sizeof(buf),"%.1f°",feels_like(w)); lv_label_set_text(s_lbl_gfeels,buf); }
    if (w.has_humidity && s_lbl_humid) { snprintf(buf,sizeof(buf),"%.0f%%",w.humidity); lv_label_set_text(s_lbl_humid,buf); }
    if (w.has_dir) {
        if (s_img_compass) lv_image_set_src(s_img_compass, CMP[dir_idx(w.wind_dir)]);
        if (s_lbl_wdir)    { snprintf(buf,sizeof(buf),"%s (%d°)",dir8(w.wind_dir),w.wind_dir); lv_label_set_text(s_lbl_wdir,buf); }
    }
    if (w.has_wind && s_lbl_metric[0]) { snprintf(buf,sizeof(buf),"%.1f km/h",w.wind_speed); lv_label_set_text(s_lbl_metric[0],buf); }
    if (w.has_gust && s_lbl_gust)      { snprintf(buf,sizeof(buf),"Böen %.1f km/h",w.wind_gust); lv_label_set_text(s_lbl_gust,buf); }
    if (w.has_rain && s_lbl_metric[1]) { snprintf(buf,sizeof(buf),"%.1f mm/h",w.rain_rate); lv_label_set_text(s_lbl_metric[1],buf); }
    if (s_lbl_precip)                  { snprintf(buf,sizeof(buf),"Heute %.1f mm",w.precip_total); lv_label_set_text(s_lbl_precip,buf); }
    if (w.has_uv) {
        if (s_lbl_metric[2]) { snprintf(buf,sizeof(buf),"%.0f",w.uv); lv_label_set_text(s_lbl_metric[2],buf); }
        if (s_lbl_uvtxt)     lv_label_set_text(s_lbl_uvtxt, uv_word(w.uv));
        if (s_uv_needle) {
            float uv=w.uv; if(uv<0)uv=0; if(uv>11)uv=11;
            float ang=3.14159265f*(1.0f-uv/11.0f); float L=48.f;
            s_uvpts[0].x=s_uv_cx; s_uvpts[0].y=s_uv_cy;
            s_uvpts[1].x=s_uv_cx+(int)(L*cosf(ang)); s_uvpts[1].y=s_uv_cy-(int)(L*sinf(ang));
            lv_line_set_points(s_uv_needle, s_uvpts, 2);
        }
    }
    if (w.has_light && s_lbl_metric[3]) { snprintf(buf,sizeof(buf),"%.0f klx",w.light_lx/1000.f); lv_label_set_text(s_lbl_metric[3],buf); }
    if (s_lbl_sunrise && w.sunrise[0]) lv_label_set_text(s_lbl_sunrise, w.sunrise);
    if (s_lbl_sunset  && w.sunset[0])  lv_label_set_text(s_lbl_sunset,  w.sunset);
}

static void wx_reset_handles(void)
{
    if (s_wx_timer) { lv_timer_delete(s_wx_timer); s_wx_timer = nullptr; }
    s_lbl_time=s_lbl_sec=s_lbl_date=s_dot=s_lbl_sensor=s_lbl_sunrise=s_lbl_sunset=nullptr;
    s_lbl_temp=s_img_cond=s_lbl_cond=s_lbl_feels=s_lbl_humid=s_lbl_gfeels=nullptr;
    s_curve=s_img_compass=s_lbl_wdir=s_lbl_gust=s_lbl_precip=s_lbl_uvtxt=s_uv_needle=nullptr;
    for (int i=0;i<4;i++) s_lbl_metric[i]=nullptr;
    for (int i=0;i<8;i++){ s_lbl_hour[i]=s_img_hour[i]=s_lbl_htemp[i]=s_lbl_hrain[i]=s_bar[i]=nullptr; }
    for (int i=0;i<7;i++){ s_day_card[i]=s_img_day[i]=s_lbl_dwd[i]=s_lbl_ddate[i]=s_lbl_dhi[i]=s_lbl_dlo[i]=s_lbl_drain[i]=nullptr; }
}
static void wx_cleanup_cb(lv_event_t *e){ (void)e; wx_reset_handles(); }

static lv_obj_t *mk_line(lv_obj_t *p, uint32_t col, int w)
{
    lv_obj_t *ln = lv_line_create(p);
    lv_obj_set_pos(ln, 0, 0);
    lv_obj_set_style_line_color(ln, lv_color_hex(col), 0);
    lv_obj_set_style_line_width(ln, w, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    return ln;
}

bool AppWeather::run(void)
{
    esp_rom_printf("WX_ENTER run()\n");
    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* ===== Statusleiste ===== */
    lv_obj_t *top = mk_panel(root, OX, 8, 1256, 70);
    lv_obj_set_style_pad_all(top, 6, 0);
    s_lbl_time = mk_label(top, "--:--", &seg_de_48, COL_TXT, 0, 4);
    s_lbl_date = mk_label(top, "--",    &seg_de_26, COL_TXT2, 196, 18);
    /* Sensor-Badge */
    lv_obj_t *bd = lv_obj_create(top);
    lv_obj_set_pos(bd, 430, 16); lv_obj_set_size(bd, 176, 34);
    lv_obj_set_style_bg_color(bd, lv_color_hex(COL_INNER), 0);
    lv_obj_set_style_border_color(bd, lv_color_hex(0x2b3543), 0);
    lv_obj_set_style_border_width(bd, 1, 0);
    lv_obj_set_style_radius(bd, 17, 0);
    lv_obj_set_style_pad_all(bd, 0, 0);
    lv_obj_remove_flag(bd, LV_OBJ_FLAG_SCROLLABLE);
    s_dot = lv_obj_create(bd);
    lv_obj_set_size(s_dot, 12, 12); lv_obj_set_pos(s_dot, 14, 11);
    lv_obj_set_style_radius(s_dot, 6, 0); lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(COL_GREEN), 0);
    lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
    s_lbl_sensor = mk_label(bd, "SENSOR OK", &seg_de_16, COL_TXT, 34, 7);
    /* Sonnenauf/-untergang */
    mk_icon_sc(top, &wxs_sun, 840, 6, 137);
    s_lbl_sunrise = mk_label(top, "--:--", &seg_de_20, COL_TXT, 880, 2);
    mk_label(top, "Sonnenaufgang", &seg_de_14, COL_TXT2, 880, 30);
    mk_icon_sc(top, &wxs_sun, 1020, 6, 137);
    s_lbl_sunset = mk_label(top, "--:--", &seg_de_20, COL_TXT, 1060, 2);
    mk_label(top, "Sonnenuntergang", &seg_de_14, COL_TXT2, 1060, 30);

    /* ===== AKTUELL ===== */
    lv_obj_t *cur = mk_panel(root, OX, 86, 470, 298);
    lv_obj_set_style_bg_color(cur, lv_color_hex(0x2E6B4A), 0);   /* schlicht: nur Gruen */
    lv_obj_t *ab = lv_obj_create(cur);
    lv_obj_set_pos(ab, 0, 0); lv_obj_set_size(ab, 100, 30);
    lv_obj_set_style_bg_color(ab, lv_color_hex(0x1f6f43), 0);
    lv_obj_set_style_border_width(ab, 0, 0); lv_obj_set_style_radius(ab, 15, 0);
    lv_obj_set_style_pad_all(ab, 0, 0); lv_obj_remove_flag(ab, LV_OBJ_FLAG_SCROLLABLE);
    mk_label(ab, "AKTUELL", &seg_de_14, COL_TXT, 16, 6);
    s_img_cond = mk_icon_sc(cur, &wxl_cloud, 286, 8, 343);   /* 112 -> ~150px */
    s_lbl_temp = mk_label(cur, "--°", &seg_de_72, COL_TXT, 28, 24);
    s_lbl_cond = mk_label(cur, "--", &seg_de_30, COL_TXT, 30, 122);
    s_lbl_feels = mk_label(cur, "Gefühlt --°", &seg_de_20, 0xCFD6E2, 30, 164);
    lv_obj_t *chip1 = mk_panel(cur, 30, 200, 150, 62);
    lv_obj_set_style_bg_color(chip1, lv_color_hex(COL_INNER), 0); lv_obj_set_style_bg_opa(chip1, 180, 0);
    lv_obj_set_style_pad_all(chip1, 8, 0);
    mk_icon(chip1, &wxdrop, 0, 2);
    s_lbl_humid = mk_label(chip1, "--%", &seg_de_24, COL_RAIN, 32, 0);
    mk_label(chip1, "Luftfeuchte", &seg_de_14, COL_TXT2, 32, 28);
    lv_obj_t *chip2 = mk_panel(cur, 192, 200, 150, 62);
    lv_obj_set_style_bg_color(chip2, lv_color_hex(COL_INNER), 0); lv_obj_set_style_bg_opa(chip2, 180, 0);
    lv_obj_set_style_pad_all(chip2, 8, 0);
    mk_icon(chip2, &wxthermo, 0, 0);
    s_lbl_gfeels = mk_label(chip2, "--°", &seg_de_24, COL_TXT, 38, 0);
    mk_label(chip2, "Gefühlt", &seg_de_14, COL_TXT2, 38, 28);

    /* ===== HEUTE STUENDLICH ===== */
    lv_obj_t *hr = mk_panel(root, 492, 86, 776, 298);
    mk_label(hr, "HEUTE · STÜNDLICH", &seg_de_16, COL_TXT2, 0, 0);
    lv_obj_t *tabs = lv_obj_create(hr);
    lv_obj_set_pos(tabs, 600, 0); lv_obj_set_size(tabs, 152, 28);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(COL_INNER), 0);
    lv_obj_set_style_border_width(tabs, 0, 0); lv_obj_set_style_radius(tabs, 14, 0);
    lv_obj_set_style_pad_all(tabs, 2, 0); lv_obj_remove_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *tb = lv_obj_create(tabs);
    lv_obj_set_pos(tb, 0, 0); lv_obj_set_size(tb, 82, 24);
    lv_obj_set_style_bg_color(tb, lv_color_hex(0x2b6cb0), 0);
    lv_obj_set_style_border_width(tb, 0, 0); lv_obj_set_style_radius(tb, 12, 0);
    lv_obj_remove_flag(tb, LV_OBJ_FLAG_SCROLLABLE);
    mk_label(tb, "Temperatur", &seg_de_14, COL_TXT, 8, 3);
    mk_label(tabs, "Regen", &seg_de_14, COL_TXT2, 92, 5);
    /* Kurve zuerst (liegt unter den Labels) */
    s_curve = mk_line(hr, COL_AMBER, 3);
    for (int i=0;i<8;i++){ s_hx[i]=47+i*94; s_cpts[i].x=s_hx[i]; s_cpts[i].y=(CURVE_TOP+CURVE_BOT)/2; }
    lv_line_set_points(s_curve, s_cpts, 8);
    for (int i=0;i<8;i++){
        int c=s_hx[i];
        s_lbl_hour[i]  = mk_clabel(hr, "--:--", &seg_de_16, COL_TXT2, c-44, 28, 88);
        s_img_hour[i]  = mk_icon_sc(hr, &wxs_cloud, c-20, 54, 183);
        s_lbl_htemp[i] = mk_clabel(hr, "--°", &seg_de_24, COL_TXT, c-44, 100, 88);
        mk_icon(hr, &wxdrop, c-32, 206);
        s_lbl_hrain[i] = mk_clabel(hr, " ", &seg_de_16, COL_RAIN, c-6, 205, 44);
    }

    /* ===== KACHELN ===== */
    int mx[4]={0,316,632,948};
    /* WIND */
    lv_obj_t *mw = mk_panel(root, mx[0]+OX, 394, 306, 150);
    mk_label(mw, "WIND", &seg_de_16, COL_AMBER, 0, 0);
    s_img_compass = mk_icon(mw, CMP[0], 0, 30);
    s_lbl_wdir = mk_label(mw, "-- (--°)", &seg_de_20, COL_TXT, 96, 34);
    s_lbl_metric[0] = mk_label(mw, "--", &seg_de_30, COL_TXT, 96, 62);
    s_lbl_gust = mk_label(mw, "Böen --", &seg_de_16, COL_TXT2, 96, 100);
    /* REGEN */
    lv_obj_t *mr = mk_panel(root, mx[1]+OX, 394, 306, 150);
    mk_label(mr, "REGEN", &seg_de_16, COL_AMBER, 0, 0);
    mk_icon_sc(mr, &wxs_rain, 0, 28, 229);
    s_lbl_metric[1] = mk_label(mr, "--", &seg_de_30, COL_TXT, 86, 30);
    s_lbl_precip = mk_label(mr, "Heute -- mm", &seg_de_16, COL_TXT2, 86, 66);
    s_bar_base = 122;
    for (int i=0;i<8;i++) s_bar[i] = mk_bar(mr, i*30, s_bar_base, 22);
    /* UV */
    lv_obj_t *mu = mk_panel(root, mx[2]+OX, 394, 306, 150);
    mk_label(mu, "UV-INDEX", &seg_de_16, COL_AMBER, 0, 0);
    s_lbl_metric[2] = mk_label(mu, "--", &seg_de_36, COL_AMBER, 0, 44);
    s_lbl_uvtxt = mk_label(mu, "--", &seg_de_16, COL_AMBER, 0, 92);
    mk_icon(mu, &wxgauge, 120, 30);
    s_uv_cx = 200; s_uv_cy = 114;
    s_uv_needle = mk_line(mu, COL_TXT, 3);
    s_uvpts[0].x=s_uv_cx; s_uvpts[0].y=s_uv_cy; s_uvpts[1].x=s_uv_cx; s_uvpts[1].y=s_uv_cy-48;
    lv_line_set_points(s_uv_needle, s_uvpts, 2);
    /* HELLIGKEIT */
    lv_obj_t *ml = mk_panel(root, mx[3]+OX, 394, 306, 150);
    mk_label(ml, "HELLIGKEIT", &seg_de_16, COL_AMBER, 0, 0);
    mk_icon_sc(ml, &wxs_sun, 0, 40, 201);
    s_lbl_metric[3] = mk_label(ml, "--", &seg_de_30, COL_TXT, 60, 50);

    /* ===== 7-TAGE ===== */
    lv_obj_t *wk = mk_panel(root, OX, 554, 1256, 238);
    lv_obj_t *wrow = mk_flexrow(wk, 0, 0, 1232, 214);
    for (int i=0;i<7;i++){
        lv_obj_t *dc = mk_daycard(wrow, 168, 214);
        s_day_card[i] = dc;
        s_lbl_dwd[i]   = mk_flabel(dc, "--", &seg_de_24, COL_TXT);
        s_lbl_ddate[i] = mk_flabel(dc, "--", &seg_de_14, COL_TXT2);
        s_img_day[i]   = mk_ficon(dc, &wxs_cloud);
        s_lbl_dhi[i]   = mk_flabel(dc, "--°", &seg_de_36, COL_TXT);
        s_lbl_dlo[i]   = mk_flabel(dc, "--°", &seg_de_22, COL_TXT2);
        s_lbl_drain[i] = mk_rain(dc, &seg_de_20);
    }

    lv_obj_add_event_cb(cur, wx_cleanup_cb, LV_EVENT_DELETE, nullptr);
    s_last_rev = 0; s_fc_rev = 0;
    s_wx_timer = lv_timer_create(wx_update_cb, 1000, nullptr);
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
