/*
 * WiFi-Setup-Section der Settings-App.
 *
 * Eigenstaendige LVGL-UI (freie Funktion, keine Brookesia-App-Klasse), wird
 * vom Tabview in app_settings.cpp als "WiFi"-Tab eingehaengt.
 *
 * Logik-Muster adaptiert von 13_brookesia_phone_full Setting.cpp, aber auf
 * unser wifi_helper + lv_async_call aufgesetzt (kein esp_lv_adapter_lock).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Baut die WiFi-Setup-UI in den uebergebenen Parent (Tab-Container). */
void app_settings_wifi_section_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
