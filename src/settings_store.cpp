#include "settings_store.h"
#include <Preferences.h>

namespace {

Preferences prefs;
const char *NAMESPACE = "kimai";

} // namespace

namespace SettingsStore {

void begin() {
    prefs.begin(NAMESPACE, false);
}

String getWifiSsid() {
    return prefs.getString("wifi_ssid", "");
}

String getWifiPassword() {
    return prefs.getString("wifi_pass", "");
}

String getKimaiUser() {
    return prefs.getString("kimai_user", "");
}

String getKimaiToken() {
    return prefs.getString("kimai_token", "");
}

String getKimaiBaseUrl() {
    return prefs.getString("kimai_base_url", "");
}

void saveWifi(const String &ssid, const String &password) {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", password);
}

void saveUser(const String &user, const String &token) {
    prefs.putString("kimai_user", user);
    prefs.putString("kimai_token", token);
}

void saveBaseUrl(const String &baseUrl) {
    prefs.putString("kimai_base_url", baseUrl);
}

Language getLanguage() {
    uint8_t value = prefs.getUChar("language", static_cast<uint8_t>(Language::DE));
    return static_cast<Language>(value);
}

void saveLanguage(Language lang) {
    prefs.putUChar("language", static_cast<uint8_t>(lang));
}

void factoryReset() {
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.remove("kimai_user");
    prefs.remove("kimai_token");
    prefs.remove("kimai_base_url");
}

bool hasWifiCredentials() {
    return prefs.isKey("wifi_ssid") && getWifiSsid().length() > 0;
}

bool hasKimaiCredentials() {
    return prefs.isKey("kimai_token") && getKimaiToken().length() > 0;
}

bool hasBaseUrl() {
    return prefs.isKey("kimai_base_url") && getKimaiBaseUrl().length() > 0;
}

} // namespace SettingsStore

String SettingsCredentialsProvider::kimaiBaseUrl() const {
    return SettingsStore::getKimaiBaseUrl();
}

String SettingsCredentialsProvider::kimaiToken() const {
    return SettingsStore::getKimaiToken();
}
