/*
 * Diagnostics-App fuer ESP32-P4-Display (Brookesia 0.5 / LVGL v9).
 *
 * Phase-2 Strang "Diagnose":
 *  D1 Geruest (Titel)        <- aktuell
 *  D2 System-Info-Panel (FW, WiFi/IP/RSSI, Uptime, Heap, Reset-Reason, Chip)
 *  D3 Log-Terminal (esp_log -> Ringpuffer -> lv_textarea)
 *  D4 Aktive Tests (WiFi-Rescan, Internet-HEAD, NTP-Sync) je in Task + lv_async_call
 *
 * Aufbau analog app_settings (Singleton + systems::phone::App-Subclass + Plugin-Registry).
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class AppDiagnostics: public systems::phone::App {
public:
    static AppDiagnostics *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~AppDiagnostics();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    AppDiagnostics(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;

private:
    static AppDiagnostics *_instance;
};

} // namespace esp_brookesia::apps
