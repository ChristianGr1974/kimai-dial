#pragma once

#include <Arduino.h>
#include "credentials_provider.h"
#include "i18n.h"

// Persistent storage of WiFi and Kimai credentials in NVS (Preferences).
// No dependency on config.h whatsoever: all values (including the Kimai
// server URL) are set exclusively through the AP+browser setup page (see
// setup_webserver.h) and stored here in NVS - no more plaintext secret in
// the source code/repo.
namespace SettingsStore {

// Initializes Preferences (the "kimai" namespace). No migration needed
// anymore (see comment above) - purely declarative for consistency with
// the rest of the codebase (call order before WifiManager::begin() etc.).
void begin();

String getWifiSsid();
String getWifiPassword();
String getKimaiUser();
String getKimaiToken();
String getKimaiBaseUrl();

void saveWifi(const String &ssid, const String &password);
void saveUser(const String &user, const String &token);
void saveBaseUrl(const String &baseUrl);

// Device UI language (device display + web setup page share this single
// setting). Defaults to DE when not yet stored (e.g. first boot).
Language getLanguage();
void saveLanguage(Language lang);

// POSIX TZ string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3") passed to
// configTzTime() in WifiManager::startNtpSync() - this is what makes both
// the UTC offset AND the DST rule configurable from the web setup page
// instead of being hardcoded for Europe/Berlin. Defaults to Europe/Berlin
// when not yet stored.
String getTimezone();
void saveTimezone(const String &posixTz);

// Clears ALL stored values (WiFi + Kimai server URL/user/token).
// Deliberately a single "reset" instead of a separate "forget WiFi": a
// half-reset device (e.g. new WiFi but still an old, possibly no longer
// valid Kimai token) would otherwise lead to confusing intermediate states
// (see the status screen) - a clear, complete reset is easier to
// understand. After the reset, the device automatically enters setup AP
// mode on the next boot (see setup() in main.cpp).
void factoryReset();

bool hasWifiCredentials();
bool hasKimaiCredentials();
bool hasBaseUrl();

} // namespace SettingsStore

// Adapter implementing ICredentialsProvider against the free SettingsStore
// functions above - removes the direct cross-dependency from
// kimai_client.cpp to SettingsStore::getKimaiBaseUrl()/getKimaiToken(). The
// remaining SettingsStore functions (saveWifi, getWifiSsid, etc.) stay
// unchanged as free functions.
class SettingsCredentialsProvider : public ICredentialsProvider {
public:
    String kimaiBaseUrl() const override;
    String kimaiToken() const override;
};
