#pragma once

#include "app_state.h"

// Render functions per state. Most carousel-based screens here only draw
// the title/hint/background - the carousel itself (the arc of items) is
// drawn separately by the caller (main.cpp) via Carousel::render(), since
// the carousel instance lives there.
namespace UiScreens {

void renderBoot();
void renderWifiConnecting();
void renderErrorWifi(const String &message);
void renderLoadingData();
void renderErrorApi(const String &message);
void renderProjectBrowse(const AppContext &ctx);
void renderActivityBrowse(const AppContext &ctx);
// Full redraw of the session screen (call once on entry or when icon selection changes).
// Full redraw of the session screen.
void renderTrackingSession(const AppContext &ctx, unsigned long elapsedMs,
                            bool isRunning, int selectedItem);
// Partial update: only redraws the clock area (no fillScreen, no flicker).
void updateTrackingSessionClock(unsigned long elapsedMs);
// Partial update: only redraws the icon row (selection highlight changed).
void updateTrackingSessionIcons(bool isRunning, int selectedItem);
void renderStartingEntry();
// Draws the static parts of the tracking screen (project/activity/hint)
// once on entering the state - the clock itself is updated separately via
// updateTrackingClock() to avoid flicker from a full fillScreen() on every
// second change.
void renderTrackingActive(const AppContext &ctx, unsigned long elapsedSeconds);
// Updates only the clock text area (no fillScreen over the whole screen),
// called once per second while the tracking state is active.
void updateTrackingClock(unsigned long elapsedSeconds);
void renderStoppingEntry();

// --- New carousel screens ---
// Title + list for MAIN_MENU/SETTINGS_MENU now come entirely from the
// carousel sprite (see Carousel::setTitle()) - no separate frame functions
// needed anymore.
void renderWifiScanListFrame();
void renderWifiScanning(); // "Suche..." interstitial screen while WiFi.scanNetworks() runs
void renderWifiNoNetworksFound();

// --- Setup AP/LAN screens (replace the former CharPicker flow) ---
// Shows ONLY the QR code (WiFi QR format) for quick connecting via phone -
// details (SSID/password/user/...) are available separately under "Status".
void renderSetupAp(const String &apSsid, const String &apPassword);
// Shows ONLY the QR code (http://<ip>/) once the device is already
// connected to the WiFi.
void renderSetupLanInfo(const String &ipAddress);

// Text overview: user, current WiFi SSID, settings URL (empty = not on the
// LAN), API token (truncated for security reasons).
void renderSettingsStatus(const String &kimaiUser, const String &wifiSsid, const String &url,
                           const String &apiToken);

} // namespace UiScreens
