#include "wifi_manager.h"
#include <WiFi.h>
#include <time.h>
#include "settings_store.h"

namespace WifiManager {

void begin() {
    WiFi.mode(WIFI_STA);
    String ssid = SettingsStore::getWifiSsid();
    String pass = SettingsStore::getWifiPassword();
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void beginWith(const String &ssid, const String &password) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (password.isEmpty()) {
        WiFi.begin(ssid.c_str());
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool waitForConnection(unsigned long timeoutMs) {
    unsigned long start = millis();
    while (!isConnected() && (millis() - start) < timeoutMs) {
        delay(200);
    }
    return isConnected();
}

void startNtpSync(const String &posixTz) {
    // posixTz tells the C library both the standard UTC offset and the DST
    // rule (e.g. "CET-1CEST,M3.5.0,M10.5.0/3" for Europe/Berlin) - the
    // library computes the correct offset for "now" itself, so DST is
    // handled automatically instead of needing a manual seasonal flag.
    configTzTime(posixTz.c_str(), "pool.ntp.org", "time.nist.gov");
}

bool isTimeSynced() {
    time_t now = time(nullptr);
    // Before NTP sync, time() returns a value close to 0 (epoch 1970).
    // A plausible value lies well after the year 2020.
    return now > 1600000000;
}

void scanNetworks(std::vector<WifiScanResult> &outResults) {
    outResults.clear();
    // Blocking (~2-4s), per the architecture spec - MVP, no async state.
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        WifiScanResult r;
        r.ssid = WiFi.SSID(i);
        r.isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        outResults.push_back(r);
    }
    WiFi.scanDelete();
}

} // namespace WifiManager
