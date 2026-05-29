/*
 * WiFi-Setup-Section - Implementation (Brookesia 0.5 / LVGL 9)
 *
 * Adaptiert von 13_brookesia_phone_full Setting.cpp:
 *  - Listen-Layout (Panel pro Netz, SSID links, Signal rechts)
 *  - Signal-Klassifizierung (-60/-80 dBm), PSK-Erkennung
 *  - Keyboard-OK-Trick (Ready-Button Index 39) loest Connect aus
 * Aber: nutzt wifi_helper (Scan/Connect) + lv_async_call statt esp_lv_adapter_lock.
 */
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AppSettingsWiFi"
#include "esp_lib_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_settings_wifi.h"
extern "C" {
#include "wifi_helper.h"
}

#define WIFI_MAX_RESULTS   20
#define WIFI_LIST_ITEM_H   56

/* UI-Handles (Singleton-Section, static ok) */
static lv_obj_t *s_list_cont   = nullptr;   /* scrollbarer Netz-Container */
static lv_obj_t *s_status_lbl  = nullptr;   /* "Scanning..." / "X networks" / "Connected" */
static lv_obj_t *s_scan_btn    = nullptr;
static lv_obj_t *s_pw_panel    = nullptr;   /* Overlay: Passwort-Eingabe */
static lv_obj_t *s_pw_ssid_lbl = nullptr;
static lv_obj_t *s_pw_textarea = nullptr;
static lv_obj_t *s_pw_keyboard = nullptr;

/* Scan-Ergebnisse (vom Task gefuellt, im LVGL-Kontext gelesen) */
static wifi_scan_result_t s_results[WIFI_MAX_RESULTS];
static int  s_results_count = 0;
static char s_sel_ssid[33]  = {0};
static bool s_scan_running  = false;

/* RSSI -> Farbe (gruen gut, gelb mittel, rot schwach) */
static lv_color_t signal_color(int8_t rssi)
{
    if (rssi > -60) return lv_color_hex(0x36C275);   /* gut */
    if (rssi > -80) return lv_color_hex(0xE0B020);   /* mittel */
    return lv_color_hex(0xD05050);                   /* schwach */
}

/* ---- Forward decls ---- */
static void on_net_clicked(lv_event_t *e);

/* Baut die Liste neu auf (laeuft im LVGL-Kontext via lv_async_call) */
static void apply_scan_results(void *arg)
{
    s_scan_running = false;
    if (!s_list_cont) return;

    lv_obj_clean(s_list_cont);

    if (s_results_count <= 0) {
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "No networks found");
        return;
    }
    if (s_status_lbl) lv_label_set_text_fmt(s_status_lbl, "%d networks", s_results_count);

    for (int i = 0; i < s_results_count; i++) {
        lv_obj_t *row = lv_obj_create(s_list_cont);
        lv_obj_set_size(row, lv_pct(100), WIFI_LIST_ITEM_H);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_net_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *ssid = lv_label_create(row);
        lv_label_set_text(ssid, s_results[i].ssid);
        lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 0, 0);

        /* Signal-Symbol rechts, eingefaerbt; "*" wenn gesichert */
        lv_obj_t *sig = lv_label_create(row);
        lv_label_set_text(sig, s_results[i].secure ? LV_SYMBOL_WIFI " *" : LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(sig, signal_color(s_results[i].rssi), 0);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void scan_task(void *arg)
{
    int n = wifi_helper_scan(s_results, WIFI_MAX_RESULTS);
    s_results_count = (n > 0) ? n : 0;
    lv_async_call(apply_scan_results, nullptr);
    vTaskDelete(NULL);
}

static void scan_btn_cb(lv_event_t *e)
{
    if (s_scan_running) return;
    s_scan_running = true;
    if (s_status_lbl) lv_label_set_text(s_status_lbl, "Scanning...");
    if (s_list_cont)  lv_obj_clean(s_list_cont);
    xTaskCreate(scan_task, "wifi_scan", 6144, NULL, 4, NULL);
}

/* ---- Connect ---- */

static void apply_connect_result(void *arg)
{
    bool ok = (arg != nullptr);
    if (s_status_lbl) {
        if (ok) lv_label_set_text_fmt(s_status_lbl, "Connected: %s", s_sel_ssid);
        else    lv_label_set_text(s_status_lbl, "Connect failed");
    }
    if (ok && s_pw_panel) lv_obj_add_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);
}

static void connect_task(void *arg)
{
    /* s_sel_ssid steht schon; Passwort wurde vor dem Task-Start kopiert */
    char *pw = (char *)arg;
    wifi_helper_store_credentials(s_sel_ssid, pw ? pw : "");
    esp_err_t err = wifi_helper_start_blocking();
    free(pw);
    lv_async_call(apply_connect_result, (void *)(intptr_t)(err == ESP_OK ? 1 : 0));
    vTaskDelete(NULL);
}

/* Tap auf ein Netz -> Passwort-Panel zeigen, SSID merken */
static void on_net_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_results_count) return;

    strncpy(s_sel_ssid, s_results[idx].ssid, sizeof(s_sel_ssid) - 1);
    s_sel_ssid[sizeof(s_sel_ssid) - 1] = 0;

    if (s_pw_ssid_lbl) lv_label_set_text_fmt(s_pw_ssid_lbl, "Password for: %s", s_sel_ssid);
    if (s_pw_textarea) lv_textarea_set_text(s_pw_textarea, "");
    if (s_pw_panel)    lv_obj_clear_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_pw_keyboard && s_pw_textarea) lv_keyboard_set_textarea(s_pw_keyboard, s_pw_textarea);
}

/* Keyboard-Event: Ready/OK-Button (Index 39) loest Connect aus, Cancel schliesst */
static void keyboard_cb(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_target(e);
    uint32_t btn = lv_keyboard_get_selected_btn(kb);
    /* Index 39 = Ready/OK in der Standard-Keyboard-Map (wie 13er-Demo) */
    if (btn == 39) {
        const char *txt = lv_textarea_get_text(s_pw_textarea);
        char *pw = strdup(txt ? txt : "");
        if (s_status_lbl) lv_label_set_text_fmt(s_status_lbl, "Connecting to %s...", s_sel_ssid);
        xTaskCreate(connect_task, "wifi_conn", 6144, pw, 5, NULL);
    }
}

static void pw_cancel_cb(lv_event_t *e)
{
    if (s_pw_panel) lv_obj_add_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);
}

/* ---- UI-Aufbau ---- */

void app_settings_wifi_section_create(lv_obj_t *parent)
{
    /* Kopfzeile: Scan-Button + Status */
    s_scan_btn = lv_btn_create(parent);
    lv_obj_set_size(s_scan_btn, 160, 50);
    lv_obj_align(s_scan_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(s_scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(scan_lbl);

    s_status_lbl = lv_label_create(parent);
    lv_label_set_text(s_status_lbl, "Tap Scan to search");
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, 180, 16);

    /* Scrollbarer Netz-Container */
    s_list_cont = lv_obj_create(parent);
    lv_obj_set_size(s_list_cont, lv_pct(100), lv_pct(70));
    lv_obj_align(s_list_cont, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list_cont, 6, 0);

    /* Passwort-Overlay (initial versteckt) */
    s_pw_panel = lv_obj_create(parent);
    lv_obj_set_size(s_pw_panel, lv_pct(100), lv_pct(100));
    lv_obj_center(s_pw_panel);
    lv_obj_set_style_bg_color(s_pw_panel, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(s_pw_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_pw_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pw_panel, LV_OBJ_FLAG_HIDDEN);

    s_pw_ssid_lbl = lv_label_create(s_pw_panel);
    lv_label_set_text(s_pw_ssid_lbl, "Password:");
    lv_obj_align(s_pw_ssid_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pw_textarea = lv_textarea_create(s_pw_panel);
    lv_textarea_set_one_line(s_pw_textarea, true);
    lv_textarea_set_password_mode(s_pw_textarea, true);
    lv_obj_set_width(s_pw_textarea, lv_pct(70));
    lv_obj_align(s_pw_textarea, LV_ALIGN_TOP_LEFT, 0, 40);

    lv_obj_t *cancel_btn = lv_btn_create(s_pw_panel);
    lv_obj_set_size(cancel_btn, 120, 50);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_RIGHT, 0, 35);
    lv_obj_add_event_cb(cancel_btn, pw_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_center(cancel_lbl);

    s_pw_keyboard = lv_keyboard_create(s_pw_panel);
    lv_obj_set_size(s_pw_keyboard, lv_pct(100), lv_pct(50));
    lv_obj_align(s_pw_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_pw_keyboard, s_pw_textarea);
    lv_obj_add_event_cb(s_pw_keyboard, keyboard_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
