/*
 * Diagnostics-App - Implementation (Brookesia 0.5 / LVGL v9)
 * D2: System-Info-Panel (Timer 2s).
 * D3: Log-Terminal (esp_log_set_vprintf -> Ringpuffer -> scrollbares Label, Timer 1s, Clear-Btn).
 *     Hook wird einmal im Ctor (Boot via Registry) installiert; vorheriger Logger wird durchgekettet.
 * D4 (aktive Tests) folgt.
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
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdarg.h>
#include <stdio.h>
#include "app_diagnostics.hpp"
extern "C" {
#include "wifi_helper.h"
#include "ota_updater.h"
}

#define APP_NAME "Diagnostics"
#define DIAG_RING_SZ 4096

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(app_diagnostics_icon_112_112);

namespace esp_brookesia::apps {

/* ---- Log ring buffer + esp_log hook ---- */
static EXT_RAM_BSS_ATTR char s_ring[DIAG_RING_SZ];   /* PSRAM: schont internen RAM (Boot-OOM-Fix) */
static size_t          s_ring_head = 0;
static bool            s_ring_wrap = false;
static portMUX_TYPE    s_ring_mux  = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t  s_prev_vprintf = nullptr;
static bool            s_hook_installed = false;

static void ring_append(const char *data, int len)
{
    if (len <= 0) {
        return;
    }
    portENTER_CRITICAL(&s_ring_mux);
    for (int i = 0; i < len; i++) {
        s_ring[s_ring_head++] = data[i];
        if (s_ring_head >= DIAG_RING_SZ) {
            s_ring_head = 0;
            s_ring_wrap = true;
        }
    }
    portEXIT_CRITICAL(&s_ring_mux);
}

static int diag_log_vprintf(const char *fmt, va_list args)
{
    char tmp[256];
    va_list cp;
    va_copy(cp, args);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, cp);
    va_end(cp);
    if (n > 0) {
        ring_append(tmp, n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1);
    }
    if (s_prev_vprintf) {
        return s_prev_vprintf(fmt, args);   /* serial logging bleibt erhalten */
    }
    return 0;
}

static void install_log_hook_once(void)
{
    if (s_hook_installed) {
        return;
    }
    s_prev_vprintf = esp_log_set_vprintf(diag_log_vprintf);
    s_hook_installed = true;
}

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
    /* WICHTIG: Log-Hook NICHT hier installieren. Der Konstruktor laeuft beim Boot
     * (initAppFromRegistry), bevor das erste Frame gerendert ist. Ein globaler
     * esp_log_set_vprintf in dieser Phase verursacht einen schwarzen Bildschirm
     * (siehe alpha.2). Hook wird lazy in run() beim Oeffnen der App gesetzt. */
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
static lv_obj_t *s_log_cont  = nullptr;
static lv_obj_t *s_log_label = nullptr;

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

/* ---- log terminal ---- */
static EXT_RAM_BSS_ATTR char s_render_buf[DIAG_RING_SZ + 1];

static void log_render(void)
{
    if (!s_log_label) {
        return;
    }
    size_t out = 0;
    portENTER_CRITICAL(&s_ring_mux);
    if (s_ring_wrap) {
        for (size_t i = s_ring_head; i < DIAG_RING_SZ; i++) {
            s_render_buf[out++] = s_ring[i];
        }
        for (size_t i = 0; i < s_ring_head; i++) {
            s_render_buf[out++] = s_ring[i];
        }
    } else {
        for (size_t i = 0; i < s_ring_head; i++) {
            s_render_buf[out++] = s_ring[i];
        }
    }
    portEXIT_CRITICAL(&s_ring_mux);
    s_render_buf[out] = '\0';

    lv_label_set_text(s_log_label, s_render_buf);
    if (s_log_cont) {
        lv_obj_scroll_to_y(s_log_cont, LV_COORD_MAX, LV_ANIM_OFF);   /* auto-scroll to newest */
    }
}

static void log_timer_cb(lv_timer_t *t)
{
    log_render();
}

static void log_clear_cb(lv_event_t *e)
{
    portENTER_CRITICAL(&s_ring_mux);
    s_ring_head = 0;
    s_ring_wrap = false;
    portEXIT_CRITICAL(&s_ring_mux);
    if (s_log_label) {
        lv_label_set_text(s_log_label, "");
    }
}

bool AppDiagnostics::run(void)
{
    install_log_hook_once();   /* erst beim Oeffnen der App aktivieren, nicht beim Boot */
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    auto mk = [&](int y) -> lv_obj_t * {
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, "");
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 40, y);
        return l;
    };

    int y = 104;
    const int step = 44;
    s_l_fw     = mk(y); y += step;
    s_l_wifi   = mk(y); y += step;
    s_l_uptime = mk(y); y += step;
    s_l_heap   = mk(y); y += step;
    s_l_psram  = mk(y); y += step;
    s_l_reset  = mk(y); y += step;
    s_l_chip   = mk(y); y += step;
    s_l_idf    = mk(y); y += step;

    /* log section header + clear button */
    lv_obj_t *log_hdr = lv_label_create(scr);
    lv_label_set_text(log_hdr, "Log");
    lv_obj_align(log_hdr, LV_ALIGN_TOP_LEFT, 40, 470);

    lv_obj_t *clr = lv_btn_create(scr);
    lv_obj_set_size(clr, 170, 56);
    lv_obj_align(clr, LV_ALIGN_TOP_RIGHT, -30, 460);
    lv_obj_add_event_cb(clr, log_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clr_lbl = lv_label_create(clr);
    lv_label_set_text(clr_lbl, "Clear log");
    lv_obj_center(clr_lbl);

    /* scrollable log container */
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 760, 600);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 530);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    s_log_cont = cont;

    lv_obj_t *lg = lv_label_create(cont);
    lv_label_set_long_mode(lg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lg, 712);
    lv_label_set_text(lg, "");
    s_log_label = lg;

    refresh_info();
    log_render();
    lv_timer_create(refresh_timer_cb, 2000, nullptr);
    lv_timer_create(log_timer_cb, 1000, nullptr);

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
