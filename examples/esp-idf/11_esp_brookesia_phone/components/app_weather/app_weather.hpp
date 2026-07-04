/*
 * Wetter-App fuer ESP32-P4-Display (Brookesia 0.5).
 *
 * Phase-2-Schritt 1: UI-Huelle nach Design-Vorlage (Querformat 1280x800, Dark).
 *   - Topbar: Uhr/Datum, Standort, Sensor-Status, Sonnenauf-/untergang
 *   - AKTUELL: grosse Temperatur, Zustand, gefuehlt, Luftfeuchte
 *   - HEUTE STUENDLICH: 8 Stunden + Temperaturkurve + Regen-%
 *   - Metriken: Wind, Regen, UV, Helligkeit/Solar
 *   - 7-TAGE-VORHERSAGE
 *
 * Daten sind in diesem Schritt Platzhalter (statisch). Anbindung folgt separat.
 * Aufbau analog app_settings (Singleton + App-Subclass + Plugin-Registrierung).
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class AppWeather: public systems::phone::App {
public:
    static AppWeather *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~AppWeather();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    AppWeather(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;

private:
    static AppWeather *_instance;
};

} // namespace esp_brookesia::apps
