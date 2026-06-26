#pragma once

#include <Arduino.h>
#include <vector>

namespace WifiManager {

struct WifiScanResult {
    String ssid;
    bool isOpen = false;
};

// Starts WiFi.begin() with the credentials from SettingsStore (NVS).
void begin();

// Starts WiFi.begin() with explicitly passed credentials (for the
// WIFI_CONNECT_TEST flow, before new credentials are saved).
void beginWith(const String &ssid, const String &password);

// Non-blocking status check. Returns true once connected.
bool isConnected();

// Blocks until connected or the timeout (ms) is reached. Returns true on success.
bool waitForConnection(unsigned long timeoutMs);

// Starts NTP sync (configTzTime) using the given POSIX TZ string (e.g.
// "CET-1CEST,M3.5.0,M10.5.0/3" for Europe/Berlin - see SettingsStore::
// getTimezone()). Should be called after a successful WiFi connection,
// BEFORE the first timesheet is started.
void startNtpSync(const String &posixTz);

// Checks whether the system time has been set plausibly (i.e. NTP synced).
bool isTimeSynced();

// Blocking WiFi scan (WiFi.scanNetworks()), takes ~2-4s. Fills outResults
// with all found SSIDs. Deliberately blocking for the MVP (no dedicated
// async state), per the architecture spec.
void scanNetworks(std::vector<WifiScanResult> &outResults);

} // namespace WifiManager
