/*
 * Settings-App fuer ESP32-P4-Display (Brookesia 0.5).
 *
 * Phase-1-MVP:
 *  - WiFi: Scan, Auswahl, Passwort-Eingabe, Save+Connect
 *  - OTA: Auto-Update-Toggle, Intervall-Anzeige, FW-Version, "Jetzt pruefen"
 *
 * Aufgebaut analog zur Brookesia SquarelineDemo-Vorlage (Singleton + App-Subclass).
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class AppSettings: public systems::phone::App {
public:
    static AppSettings *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~AppSettings();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    AppSettings(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;

private:
    static AppSettings *_instance;
};

} // namespace esp_brookesia::apps
