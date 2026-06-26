#pragma once

#include <Arduino.h>

// Web-based settings page that replaces the former rotary character picker
// (char_picker.h/.cpp). Instead of entering letter-by-letter with the
// encoder, WiFi/Kimai credentials are configured through a simple HTML
// form in the browser - via phone connected to the device's AP, or
// directly on the LAN if the device is already connected.
//
// The web server runs in the background independent of the current UI
// state; main.cpp calls SetupWebServer::handleClient() on every loop()
// tick, regardless of which AppState is currently active (except BOOT).
namespace SetupWebServer {

// Fixed AP credentials for the setup access point. Deliberately a fixed
// password (not a random value): the device has no display that could
// persistently show a randomly generated password at boot time, and a
// fixed, documented password is pragmatically sufficient for the threat
// model of a "short-lived local setup AP".
constexpr const char *AP_SSID = "KimaiDial-Setup";
constexpr const char *AP_PASSWORD = "kimaidial";

// Starts the access point (WiFi.softAP) AND the web server. Called when
// the device is not connected to the LAN when settings are opened.
void startAccessPoint();

// Starts ONLY the web server (no AP) - for the case where the device is
// already connected to the LAN via WiFi.begin(), but the settings page
// should still be reachable on that same LAN.
void startInStationMode();

// Stops the AP again (WiFi.softAPdisconnect), e.g. when leaving SETUP_AP
// via long press. The web server itself keeps running (it isn't tied to
// the AP, only to the respective network interface).
void stopAccessPoint();

// Must be called on every loop() tick once the server has been started
// (regardless of app state) - otherwise incoming HTTP requests are not
// processed.
void handleClient();

// true once start*() has been called (server is running).
bool isRunning();

} // namespace SetupWebServer
