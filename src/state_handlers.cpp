// SPIFFS.h before M5Unified.h/M5Dial.h, see comment in ui_screens.cpp.
#include <SPIFFS.h>
#include <M5Unified.h>
#include <M5Dial.h>
#include <WiFi.h>
#include "app_state.h"
#include "settings_store.h"
#include "wifi_manager.h"
#include "kimai_client.h"
#include "ui_screens.h"
#include "carousel.h"
#include "setup_webserver.h"
#include "state_handlers.h"
#include "i18n.h"
#include <time.h>

namespace {

AppContext ctx;

// Single adapter that gives the free SettingsStore functions an
// ICredentialsProvider interface - injected into all KimaiClient calls
// (dependency inversion: kimai_client.cpp no longer knows SettingsStore directly).
SettingsCredentialsProvider g_credentialsProvider;

const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long LONG_PRESS_MS = 600;

// Sentinel payloadId for the explicit "back" carousel item that all browse
// lists (settings/projects/activities/WiFi scan) offer in addition to long
// press - real Kimai IDs and WiFi scan indices are always >= 0, so a
// negative sentinel here can't collide.
constexpr int BACK_ITEM_ID = -100;

unsigned long wifiConnectStart = 0;

bool needsRender = true; // forces an initial draw on every state change

// A single generic carousel instance, repopulated depending on the state
// (MAIN_MENU, SETTINGS_MENU, LIST_BROWSE, ACTIVITY_BROWSE). Since only one
// carousel screen is ever active at a time, a shared instance is enough -
// setItems() is called again on every state entry.
Carousel carousel;

// Long-press tracking for browse states (main menu/settings/projects/
// activities/WiFi scan list). Custom timing instead of M5Unified's
// pressedFor(), to stay independent of the exact API version.
unsigned long pressStartMillis = 0;
bool pressTracking = false;
bool longPressFired = false;

void setState(AppState newState) {
    ctx.state = newState;
    needsRender = true;
}

// Reads the encoder delta since the last call. M5Dial.Encoder.read()
// returns the absolute counter value; we track the difference ourselves.
long lastEncoderValue = 0;

// The M5Dial encoder delivers 4 raw ticks per tactile detent (one full
// quadrature period). With a divisor of 2 the selection jumped 2 items per
// detent - empirically confirmed on the device that 4 is the correct
// value. We accumulate raw ticks in a remainder accumulator and only
// report whole detent steps outward, so exactly one detent equals exactly
// one list step.
constexpr long ENCODER_TICKS_PER_STEP = 4;
long encoderTickAccumulator = 0;

long readEncoderDelta() {
    long current = M5Dial.Encoder.read();
    long rawDelta = current - lastEncoderValue;
    lastEncoderValue = current;

    encoderTickAccumulator += rawDelta;
    long steps = encoderTickAccumulator / ENCODER_TICKS_PER_STEP;
    encoderTickAccumulator -= steps * ENCODER_TICKS_PER_STEP;
    return steps;
}

bool wasClicked() {
    // Both the encoder button and touch button A are accepted as a "click",
    // since depending on the M5Unified version the encoder click can be mapped to BtnA.
    return M5Dial.BtnA.wasClicked();
}

// Checks for a long press since the last button-down event. Returns true
// EXACTLY ONCE when the threshold is exceeded (prevents firing repeatedly
// while the button continues to be held).
bool wasLongPressed() {
    bool isPressed = M5Dial.BtnA.isPressed();
    if (isPressed && !pressTracking) {
        pressTracking = true;
        longPressFired = false;
        pressStartMillis = millis();
    } else if (!isPressed) {
        pressTracking = false;
        longPressFired = false;
    } else if (pressTracking && !longPressFired && (millis() - pressStartMillis) >= LONG_PRESS_MS) {
        longPressFired = true;
        return true;
    }
    return false;
}

void resolveActiveNamesFromProjects(int projectId, int activityId) {
    ctx.tracking.activeProjectName = "";
    ctx.tracking.activeActivityName = "";
    ctx.tracking.activeColorHex = "";
    for (auto &p : ctx.selection.projects) {
        if (p.id == projectId) {
            ctx.tracking.activeProjectName = p.name;
            ctx.tracking.activeColorHex = p.colorHex; // activity list isn't loaded here, project color as fallback
            break;
        }
    }
    if (ctx.tracking.activeProjectName.isEmpty()) {
        ctx.tracking.activeProjectName = String(I18n::t(I18n::Key::PROJECT_TITLE)) + " #" + String(projectId);
    }
    // We don't necessarily know the activity name without loading the
    // activity list; for the resume case the ID is a sufficient fallback.
    ctx.tracking.activeActivityName = String(I18n::t(I18n::Key::ACTIVITY_TITLE)) + " #" + String(activityId);
}

// Parses a Kimai "begin" timestamp ("YYYY-MM-DDTHH:MM:SS", no timezone
// suffix - Kimai stores/returns it as local time, same convention used when
// sending it in startTimesheet()) into epoch seconds, so a resumed timesheet
// (after a device reboot) can show the true elapsed duration instead of
// restarting the stopwatch at zero. Returns -1 if the string can't be parsed.
long parseLocalTimestampToEpoch(const String &timestamp) {
    struct tm tmStruct = {};
    if (strptime(timestamp.c_str(), "%Y-%m-%dT%H:%M:%S", &tmStruct) == nullptr) {
        return -1;
    }
    tmStruct.tm_isdst = -1; // let mktime() decide DST based on the date itself
    return (long)mktime(&tmStruct);
}

uint16_t parseColorOrDefault(const String &hex, uint16_t fallback) {
    if (hex.length() != 7 || hex[0] != '#') {
        return fallback;
    }
    long value = strtol(hex.c_str() + 1, nullptr, 16);
    uint8_t r = (value >> 16) & 0xFF;
    uint8_t g = (value >> 8) & 0xFF;
    uint8_t b = value & 0xFF;
    return M5Dial.Display.color565(r, g, b);
}

CarouselItem makeBackItem() {
    CarouselItem back;
    back.label = I18n::t(I18n::Key::BACK);
    back.payloadId = BACK_ITEM_ID;
    return back;
}

void buildProjectCarousel() {
    std::vector<CarouselItem> items;
    for (auto &p : ctx.selection.projects) {
        CarouselItem item;
        item.label = p.name;
        item.hasColor = !p.colorHex.isEmpty();
        item.color = parseColorOrDefault(p.colorHex, 0);
        item.payloadId = p.id;
        items.push_back(item);
    }
    items.push_back(makeBackItem());
    carousel.setTitle(I18n::t(I18n::Key::PROJECT_TITLE));
    carousel.setItems(items);
    carousel.setSelectedIndex(ctx.selection.selectedProjectIndex);
}

void buildActivityCarousel() {
    std::vector<CarouselItem> items;
    for (auto &a : ctx.selection.activities) {
        CarouselItem item;
        item.label = a.name;
        item.hasColor = !a.colorHex.isEmpty();
        item.color = parseColorOrDefault(a.colorHex, 0);
        item.payloadId = a.id;
        items.push_back(item);
    }
    items.push_back(makeBackItem());
    carousel.setTitle(I18n::t(I18n::Key::ACTIVITY_TITLE));
    carousel.setItems(items);
    carousel.setSelectedIndex(ctx.selection.selectedActivityIndex);
}

void buildMainMenuCarousel() {
    std::vector<CarouselItem> items;
    CarouselItem track;
    track.label = I18n::t(I18n::Key::MAIN_MENU_TRACKING);
    track.payloadId = 0;
    track.hasColor = true;
    track.color = M5Dial.Display.color565(0x2E, 0x86, 0xDE); // blue
    // Without a Kimai server URL/user/token, a tap here would just be an
    // HTTP error - show it disabled instead, click redirects to settings
    // (see handleMainMenu()).
    track.disabled = !SettingsStore::hasKimaiCredentials() || !SettingsStore::hasBaseUrl();
    items.push_back(track);

    CarouselItem settings;
    settings.label = I18n::t(I18n::Key::MAIN_MENU_SETTINGS);
    settings.payloadId = 1;
    settings.hasColor = true;
    settings.color = M5Dial.Display.color565(0x8E, 0x44, 0xAD); // violet
    items.push_back(settings);

    carousel.setTitle(I18n::t(I18n::Key::MAIN_MENU_TITLE));
    carousel.setItems(items);
    carousel.setSelectedIndex(0);
}

// "Setup" (just the QR code for connecting/configuring) and "Status" (text
// overview of user/WiFi/URL/token) are deliberately separate: setup should
// quickly and cleanly show just the QR code on the device, status is the
// place for the detailed diagnostic display.
constexpr int SETTINGS_SETUP_ID = 0;
constexpr int SETTINGS_STATUS_ID = 1;
constexpr int SETTINGS_LANGUAGE_ID = 2;

void buildSettingsMenuCarousel() {
    std::vector<CarouselItem> items;
    CarouselItem setup;
    setup.label = I18n::t(I18n::Key::SETTINGS_SETUP);
    setup.payloadId = SETTINGS_SETUP_ID;
    setup.hasColor = true;
    setup.color = M5Dial.Display.color565(0x16, 0xA0, 0x85); // teal
    items.push_back(setup);

    CarouselItem status;
    status.label = I18n::t(I18n::Key::SETTINGS_STATUS);
    status.payloadId = SETTINGS_STATUS_ID;
    status.hasColor = true;
    status.color = M5Dial.Display.color565(0x2C, 0x3E, 0x50); // dark blue-gray
    items.push_back(status);

    // Toggles DE<->EN on click (see handleSettingsMenu()) - only two
    // languages, so a dedicated sub-menu state would be overengineering.
    CarouselItem language;
    language.label = I18n::t(I18n::Key::SETTINGS_LANGUAGE);
    language.payloadId = SETTINGS_LANGUAGE_ID;
    language.hasColor = true;
    language.color = M5Dial.Display.color565(0xE6, 0x7E, 0x22); // orange
    items.push_back(language);

    items.push_back(makeBackItem());
    carousel.setTitle(I18n::t(I18n::Key::SETTINGS_MENU_TITLE));
    carousel.setItems(items);
    carousel.setSelectedIndex(0);
}

void loadProjectsAndCheckActive() {
    // Without a Kimai server URL/user/token, the API call would be
    // pointless anyway and would only show a confusing HTTP error
    // ("connection failed" or similar) - go straight to the main menu
    // instead, where "Zeiterfassung" is marked disabled (see
    // buildMainMenuCarousel()) and the click redirects to settings.
    if (!SettingsStore::hasKimaiCredentials() || !SettingsStore::hasBaseUrl()) {
        ctx.selection.selectedProjectIndex = 0;
        setState(AppState::MAIN_MENU);
        return;
    }

    setState(AppState::LOADING_DATA);
    UiScreens::renderLoadingData();

    String err;
    if (!KimaiClient::fetchProjects(g_credentialsProvider, ctx.selection.projects, err)) {
        ctx.error.errorMessage = err;
        ctx.error.lastFailedAction = LastAction::LOAD_PROJECTS;
        setState(AppState::ERROR_API);
        return;
    }

    if (ctx.selection.projects.empty()) {
        // No real error to retry here (no project exists yet in Kimai) -
        // lastFailedAction = NONE makes handleErrorApi() go straight back
        // to the main menu on click instead of looping the same fetch.
        ctx.error.errorMessage = I18n::t(I18n::Key::PROJECT_EMPTY);
        ctx.error.lastFailedAction = LastAction::NONE;
        setState(AppState::ERROR_API);
        return;
    }
    ctx.selection.projectsLoaded = true;

    // Check whether a timesheet is already running (resume after reboot).
    int activeId = -1, activeProjectId = -1, activeActivityId = -1;
    String activeBegin;
    String activeErr;
    if (KimaiClient::fetchActiveTimesheet(g_credentialsProvider, activeId, activeProjectId, activeActivityId, activeBegin, activeErr)) {
        if (activeId != -1) {
            ctx.tracking.activeTimesheetId = activeId;
            resolveActiveNamesFromProjects(activeProjectId, activeActivityId);
            // Reconstruct the true elapsed duration from Kimai's "begin"
            // timestamp instead of restarting the stopwatch at zero: the
            // ESP32 has no persistent monotonic clock across reboots, but
            // with NTP synced we DO have the correct wall-clock time, so we
            // can compute how long the timesheet has actually been running
            // and back-date trackingStartMillis accordingly.
            long beginEpoch = parseLocalTimestampToEpoch(activeBegin);
            if (beginEpoch > 0 && WifiManager::isTimeSynced()) {
                long elapsedSeconds = (long)time(nullptr) - beginEpoch;
                if (elapsedSeconds < 0) {
                    elapsedSeconds = 0; // clock skew/parse edge case - don't show a negative duration
                }
                ctx.tracking.trackingStartMillis = millis() - (unsigned long)elapsedSeconds * 1000UL;
            } else {
                // Fallback if the timestamp can't be parsed or NTP hasn't
                // synced yet: start the stopwatch at zero (previous behavior).
                ctx.tracking.trackingStartMillis = millis();
            }
            setState(AppState::TRACKING_ACTIVE);
            return;
        }
    }
    // If fetchActiveTimesheet fails, we deliberately ignore that here
    // (not a hard error) and go to the main menu as normal.

    ctx.selection.selectedProjectIndex = 0;
    setState(AppState::MAIN_MENU);
}

void retryLastAction() {
    switch (ctx.error.lastFailedAction) {
        case LastAction::CONNECT_WIFI:
            WifiManager::begin();
            wifiConnectStart = millis();
            setState(AppState::WIFI_CONNECTING);
            break;
        case LastAction::LOAD_PROJECTS:
            loadProjectsAndCheckActive();
            break;
        case LastAction::LOAD_ACTIVITIES: {
            setState(AppState::LOADING_DATA);
            UiScreens::renderLoadingData();
            String err;
            const KimaiProject &p = ctx.selection.projects[ctx.selection.selectedProjectIndex];
            if (!KimaiClient::fetchActivities(g_credentialsProvider, p.id, ctx.selection.activities, err)) {
                ctx.error.errorMessage = err;
                ctx.error.lastFailedAction = LastAction::LOAD_ACTIVITIES;
                setState(AppState::ERROR_API);
                return;
            }
            ctx.selection.selectedActivityIndex = 0;
            setState(AppState::ACTIVITY_BROWSE);
            break;
        }
        case LastAction::START_TIMESHEET: {
            setState(AppState::STARTING_ENTRY);
            UiScreens::renderStartingEntry();
            String err;
            int newId = -1;
            const KimaiProject &p = ctx.selection.projects[ctx.selection.selectedProjectIndex];
            const KimaiActivity &a = ctx.selection.activities[ctx.selection.selectedActivityIndex];
            if (!KimaiClient::startTimesheet(g_credentialsProvider, p.id, a.id, newId, err)) {
                ctx.error.errorMessage = err;
                ctx.error.lastFailedAction = LastAction::START_TIMESHEET;
                setState(AppState::ERROR_API);
                return;
            }
            ctx.tracking.activeTimesheetId = newId;
            ctx.tracking.activeProjectName = p.name;
            ctx.tracking.activeActivityName = a.name;
            ctx.tracking.activeColorHex = !a.colorHex.isEmpty() ? a.colorHex : p.colorHex;
            ctx.tracking.trackingStartMillis = millis();
            setState(AppState::TRACKING_ACTIVE);
            break;
        }
        case LastAction::STOP_TIMESHEET: {
            setState(AppState::STOPPING_ENTRY);
            UiScreens::renderStoppingEntry();
            String err;
            if (!KimaiClient::stopTimesheet(g_credentialsProvider, ctx.tracking.activeTimesheetId, err)) {
                // Even on retry: don't block forever, show it clearly and go back.
                ctx.error.errorMessage = I18n::currentLanguage() == Language::DE
                                              ? "Stop fehlgeschlagen, bitte im Web-UI pruefen"
                                              : "Stop failed, please check the web UI";
                ctx.tracking.activeTimesheetId = -1;
                setState(AppState::ERROR_API);
                ctx.error.lastFailedAction = LastAction::NONE;
                return;
            }
            ctx.tracking.activeTimesheetId = -1;
            ctx.offlineNotice = false;
            setState(AppState::MAIN_MENU);
            break;
        }
        default:
            break;
    }
}

// --- Carousel-based state handlers ---

// Advances the animation and reports whether a redraw is needed at all.
// Without this gating, carousel.render() would redraw completely on every
// tick (every ~20ms) - including reloading the font from SPIFFS for the
// label - even if nothing is moving. That caused visible flicker. We
// therefore only render: on state entry (justEntered), while the animation
// is running, and exactly once after it finishes (so the final
// position + label still get drawn).
bool tickCarouselAndShouldRender(bool justEntered) {
    bool wasAnimating = carousel.isAnimating();
    carousel.update();
    bool stillAnimating = carousel.isAnimating();
    return justEntered || wasAnimating || stillAnimating;
}

// Encapsulates the identical boilerplate part of all carousel screens
// (handleMainMenu/handleSettingsMenu/handleListBrowse/handleActivityBrowse):
// build the carousel on state entry, render it per tick, read the encoder
// delta. A plain function pointer instead of std::function - no heap, no
// closure overhead in the 20ms loop. The state-specific click/long-press
// logic deliberately stays in the respective handler (differs per screen
// and couldn't be meaningfully generalized).
long enterCarouselScreen(void (*buildFn)()) {
    bool justEntered = needsRender;
    if (needsRender) {
        buildFn();
        needsRender = false;
    }
    if (tickCarouselAndShouldRender(justEntered)) {
        carousel.render();
    }
    return readEncoderDelta();
}

void enterListBrowse() {
    UiScreens::renderProjectBrowse(ctx);
    buildProjectCarousel();
}

void enterActivityBrowse() {
    UiScreens::renderActivityBrowse(ctx);
    buildActivityCarousel();
}

void handleMainMenu() {
    long delta = enterCarouselScreen(buildMainMenuCarousel);
    if (delta != 0) {
        carousel.onEncoderDelta(delta);
    }

    if (wasClicked() && !carousel.empty()) {
        int idx = carousel.selectedIndex();
        if (idx == 0 && carousel.selectedItem().disabled) {
            // "Zeiterfassung" is disabled (no Kimai server URL/user/token
            // stored) - go straight to settings instead of provoking a
            // pointless API call with an HTTP error.
            setState(AppState::SETTINGS_MENU);
        } else if (idx == 0) {
            // "Zeiterfassung": load projects briefly if not loaded yet.
            if (!ctx.selection.projectsLoaded) {
                loadProjectsAndCheckActive();
                // loadProjectsAndCheckActive() sets MAIN_MENU or TRACKING_ACTIVE;
                // if MAIN_MENU (no active timesheet), proceed straight to LIST_BROWSE.
                if (ctx.state == AppState::MAIN_MENU) {
                    setState(AppState::LIST_BROWSE);
                }
            } else {
                setState(AppState::LIST_BROWSE);
            }
        } else {
            setState(AppState::SETTINGS_MENU);
        }
    }
    // No long-press-back: MAIN_MENU is the root.
    wasLongPressed();
}

void handleSettingsMenu() {
    long delta = enterCarouselScreen(buildSettingsMenuCarousel);
    if (delta != 0) {
        carousel.onEncoderDelta(delta);
    }

    if (wasClicked() && !carousel.empty()) {
        const CarouselItem &item = carousel.selectedItem();
        if (item.payloadId == BACK_ITEM_ID) {
            setState(AppState::MAIN_MENU);
        } else if (item.payloadId == SETTINGS_STATUS_ID) {
            setState(AppState::SETTINGS_STATUS);
        } else if (item.payloadId == SETTINGS_LANGUAGE_ID) {
            // Only two languages - a simple toggle is enough, no dedicated
            // sub-menu state needed. Re-entering SETTINGS_MENU (same state)
            // forces the carousel to rebuild via the existing
            // needsRender/setState() mechanism, so the labels redraw in the
            // newly selected language immediately.
            Language newLang = (I18n::currentLanguage() == Language::DE) ? Language::EN : Language::DE;
            I18n::setLanguage(newLang);
            SettingsStore::saveLanguage(newLang);
            setState(AppState::SETTINGS_MENU);
        } else {
            // Setup entry: depending on the current connection status,
            // either AP mode (not yet on the LAN) or the LAN info screen
            // (already connected) - the web server itself runs in both
            // cases, only the AP part differs.
            if (WifiManager::isConnected()) {
                SetupWebServer::startInStationMode();
                setState(AppState::SETUP_LAN_INFO);
            } else {
                SetupWebServer::startAccessPoint();
                setState(AppState::SETUP_AP);
            }
        }
        return;
    }
    if (wasLongPressed()) {
        setState(AppState::MAIN_MENU);
    }
}

void handleListBrowse() {
    long delta = enterCarouselScreen(enterListBrowse);
    if (delta != 0) {
        carousel.onEncoderDelta(delta);
        // Don't sync when the "back" item (appended after the projects) is
        // selected - its index lies outside ctx.selection.projects and
        // would otherwise cause an out-of-bounds access.
        int sel = carousel.selectedIndex();
        if (sel < (int)ctx.selection.projects.size()) {
            ctx.selection.selectedProjectIndex = sel;
        }
    }

    if (wasClicked() && !ctx.selection.projects.empty()) {
        const CarouselItem &item = carousel.selectedItem();
        if (item.payloadId == BACK_ITEM_ID) {
            setState(AppState::MAIN_MENU);
            return;
        }
        ctx.selection.selectedProjectIndex = carousel.selectedIndex();
        setState(AppState::LOADING_DATA);
        UiScreens::renderLoadingData();
        String err;
        const KimaiProject &p = ctx.selection.projects[ctx.selection.selectedProjectIndex];
        if (!KimaiClient::fetchActivities(g_credentialsProvider, p.id, ctx.selection.activities, err)) {
            ctx.error.errorMessage = err;
            ctx.error.lastFailedAction = LastAction::LOAD_ACTIVITIES;
            setState(AppState::ERROR_API);
            return;
        }
        if (ctx.selection.activities.empty()) {
            // Same reasoning as the empty-projects case above: nothing to
            // retry, go straight back to the main menu on click.
            ctx.error.errorMessage = I18n::t(I18n::Key::ACTIVITY_EMPTY);
            ctx.error.lastFailedAction = LastAction::NONE;
            setState(AppState::ERROR_API);
            return;
        }
        ctx.selection.selectedActivityIndex = 0;
        setState(AppState::ACTIVITY_BROWSE);
        return;
    }
    if (wasLongPressed()) {
        setState(AppState::MAIN_MENU);
    }
}

void handleActivityBrowse() {
    long delta = enterCarouselScreen(enterActivityBrowse);
    if (delta != 0) {
        carousel.onEncoderDelta(delta);
        int sel = carousel.selectedIndex();
        if (sel < (int)ctx.selection.activities.size()) {
            ctx.selection.selectedActivityIndex = sel;
        }
    }

    if (wasClicked() && !ctx.selection.activities.empty()) {
        const CarouselItem &item = carousel.selectedItem();
        if (item.payloadId == BACK_ITEM_ID) {
            setState(AppState::LIST_BROWSE);
            return;
        }
        ctx.selection.selectedActivityIndex = carousel.selectedIndex();
        setState(AppState::STARTING_ENTRY);
        UiScreens::renderStartingEntry();
        String err;
        int newId = -1;
        const KimaiProject &p = ctx.selection.projects[ctx.selection.selectedProjectIndex];
        const KimaiActivity &a = ctx.selection.activities[ctx.selection.selectedActivityIndex];
        if (!KimaiClient::startTimesheet(g_credentialsProvider, p.id, a.id, newId, err)) {
            ctx.error.errorMessage = err;
            ctx.error.lastFailedAction = LastAction::START_TIMESHEET;
            setState(AppState::ERROR_API);
            return;
        }
        ctx.tracking.activeTimesheetId = newId;
        ctx.tracking.activeProjectName = p.name;
        ctx.tracking.activeActivityName = a.name;
        ctx.tracking.activeColorHex = !a.colorHex.isEmpty() ? a.colorHex : p.colorHex;
        ctx.tracking.trackingStartMillis = millis();
        setState(AppState::TRACKING_ACTIVE);
        return;
    }
    if (wasLongPressed()) {
        // Only one step back (to the project list), don't jump all the
        // way to the main menu - ACTIVITY_BROWSE sits one level below LIST_BROWSE.
        setState(AppState::LIST_BROWSE);
    }
}

void handleTrackingActive() {
    // The encoder is deliberately ignored here (by design), only a click stops.
    static unsigned long lastDisplayedSeconds = (unsigned long)-1;
    static bool lastOfflineNotice = false;

    ctx.offlineNotice = !WifiManager::isConnected();

    unsigned long elapsedSeconds = (millis() - ctx.tracking.trackingStartMillis) / 1000;
    // Only redraw when the displayed second or the offline status changes -
    // otherwise the display flickers from fillScreen() on every loop()
    // iteration (every 20ms) even though the text isn't actually changing.
    if (needsRender || ctx.offlineNotice != lastOfflineNotice) {
        // Full redraw only on state entry or when the offline hint changes
        // (it sits outside the clock area).
        UiScreens::renderTrackingActive(ctx, elapsedSeconds);
        lastDisplayedSeconds = elapsedSeconds;
        lastOfflineNotice = ctx.offlineNotice;
        needsRender = false;
    } else if (elapsedSeconds != lastDisplayedSeconds) {
        // Only partially update the clock - no fillScreen, no flicker.
        UiScreens::updateTrackingClock(elapsedSeconds);
        lastDisplayedSeconds = elapsedSeconds;
    }

    if (wasClicked()) {
        setState(AppState::STOPPING_ENTRY);
        UiScreens::renderStoppingEntry();
        String err;
        if (!KimaiClient::stopTimesheet(g_credentialsProvider, ctx.tracking.activeTimesheetId, err)) {
            ctx.error.errorMessage = "Stop fehlgeschlagen, bitte im Web-UI pruefen";
            ctx.error.lastFailedAction = LastAction::STOP_TIMESHEET;
            // Despite the error, don't block in an infinite loop: we show
            // the error briefly and then return to the main menu, the
            // local tracking state is reset.
            setState(AppState::ERROR_API);
            return;
        }
        ctx.tracking.activeTimesheetId = -1;
        ctx.offlineNotice = false;
        setState(AppState::MAIN_MENU);
    }
}

void handleErrorApi() {
    if (needsRender) {
        UiScreens::renderErrorApi(ctx.error.errorMessage);
        needsRender = false;
    }
    if (wasClicked()) {
        // Special case: a stop error must NOT block forever - on click,
        // simply return to the main menu instead of retrying the stop, if
        // the user decides so. Retry is still the default action.
        if (ctx.error.lastFailedAction == LastAction::NONE) {
            setState(AppState::MAIN_MENU);
            return;
        }
        retryLastAction();
    }
}

void handleErrorWifi() {
    if (needsRender) {
        UiScreens::renderErrorWifi(ctx.error.errorMessage);
        needsRender = false;
    }
    if (wasClicked()) {
        // Instead of just retrying the connection: the user should have
        // the chance to change the stored credentials (per the architecture spec).
        setState(AppState::SETTINGS_MENU);
    }
}

void handleWifiConnecting() {
    if (needsRender) {
        UiScreens::renderWifiConnecting();
        needsRender = false;
    }

    if (WifiManager::isConnected()) {
        ctx.wifiConnected = true;
        WifiManager::startNtpSync();
        // Brief wait for NTP, so the first timesheet start has a plausible
        // time. We block briefly here (max 5s) instead of introducing
        // another state - keeps the state machine simple.
        unsigned long ntpStart = millis();
        while (!WifiManager::isTimeSynced() && (millis() - ntpStart) < 5000) {
            delay(100);
        }
        loadProjectsAndCheckActive();
        return;
    }

    if (millis() - wifiConnectStart > WIFI_TIMEOUT_MS) {
        ctx.error.errorMessage =
            I18n::currentLanguage() == Language::DE ? "WLAN Timeout" : "WiFi timeout";
        ctx.error.lastFailedAction = LastAction::CONNECT_WIFI;
        setState(AppState::ERROR_WIFI);
    }
}

// SETUP_AP: AP mode active (see SetupWebServer::startAccessPoint(), already
// triggered when leaving SETTINGS_MENU). Shows the SSID/password as text +
// a QR code for connecting. We deliberately leave the AP running until the
// user explicitly goes back via long press (no timeout) - simpler than a
// background timer, and the user stays in control.
void handleSetupAp() {
    if (needsRender) {
        UiScreens::renderSetupAp(SetupWebServer::AP_SSID, SetupWebServer::AP_PASSWORD);
        needsRender = false;
    }
    // A simple click is enough here to go back (no dedicated carousel item
    // possible since the screen only shows the QR code) - long press
    // remains available as well, as the familiar gesture.
    if (wasClicked() || wasLongPressed()) {
        SetupWebServer::stopAccessPoint();
        setState(AppState::SETTINGS_MENU);
    }
}

// SETUP_LAN_INFO: device is already connected to the LAN, web server runs
// in STA mode (no AP needed). Shows only the local IP as a QR code.
void handleSetupLanInfo() {
    if (needsRender) {
        UiScreens::renderSetupLanInfo(WiFi.localIP().toString());
        needsRender = false;
    }
    if (wasClicked() || wasLongPressed()) {
        setState(AppState::SETTINGS_MENU);
    }
}

// SETTINGS_STATUS: pure text overview (user/WiFi/URL/token), no QR code.
// URL is only filled in when the device is connected to the LAN (otherwise
// empty - renderSettingsStatus() then shows "(nicht im LAN)").
void handleSettingsStatus() {
    if (needsRender) {
        String url = WifiManager::isConnected() ? "http://" + WiFi.localIP().toString() + "/" : "";
        UiScreens::renderSettingsStatus(SettingsStore::getKimaiUser(), SettingsStore::getWifiSsid(), url,
                                         SettingsStore::getKimaiToken());
        needsRender = false;
    }
    if (wasClicked() || wasLongPressed()) {
        setState(AppState::SETTINGS_MENU);
    }
}

} // namespace

namespace StateMachine {

void begin() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);

    M5Dial.Display.setRotation(0);
    lastEncoderValue = M5Dial.Encoder.read();

    // For the anti-aliased VLW fonts (see ui_screens.cpp), which are
    // loaded onto the SPIFFS partition from data/ via `pio run -t uploadfs`.
    SPIFFS.begin();

    UiScreens::renderBoot();
    delay(500);

    SettingsStore::begin();
    I18n::setLanguage(SettingsStore::getLanguage());

    // Without stored WiFi credentials (e.g. first boot or after "reset"), a
    // connection attempt would be pointless anyway and would just wait out
    // the full WIFI_TIMEOUT_MS before the user could manually navigate to
    // Settings->Setup via ERROR_WIFI. Jump straight into setup AP mode
    // instead - that's exactly the screen the user needs immediately in this case.
    if (!SettingsStore::hasWifiCredentials()) {
        SetupWebServer::startAccessPoint();
        setState(AppState::SETUP_AP);
        return;
    }

    WifiManager::begin();
    wifiConnectStart = millis();
    setState(AppState::WIFI_CONNECTING);
}

void tick() {
    M5Dial.update();

    switch (ctx.state) {
        case AppState::BOOT:
            // left directly in begin(), only a fallback here
            setState(AppState::WIFI_CONNECTING);
            break;
        case AppState::WIFI_CONNECTING:
            handleWifiConnecting();
            break;
        case AppState::ERROR_WIFI:
            handleErrorWifi();
            break;
        case AppState::LOADING_DATA:
            // Rendering happens synchronously in the callers; nothing to do here.
            break;
        case AppState::ERROR_API:
            handleErrorApi();
            break;
        case AppState::LIST_BROWSE:
            handleListBrowse();
            break;
        case AppState::ACTIVITY_BROWSE:
            handleActivityBrowse();
            break;
        case AppState::STARTING_ENTRY:
            // handled synchronously in handleActivityBrowse/retry
            break;
        case AppState::TRACKING_ACTIVE:
            handleTrackingActive();
            break;
        case AppState::STOPPING_ENTRY:
            // handled synchronously in handleTrackingActive/retry
            break;
        case AppState::MAIN_MENU:
            handleMainMenu();
            break;
        case AppState::SETTINGS_MENU:
            handleSettingsMenu();
            break;
        case AppState::SETUP_AP:
            handleSetupAp();
            break;
        case AppState::SETUP_LAN_INFO:
            handleSetupLanInfo();
            break;
        case AppState::SETTINGS_STATUS:
            handleSettingsStatus();
            break;
    }

    // Web server polling: must happen on EVERY loop() tick, independent of
    // the current UI state, otherwise incoming HTTP requests won't be
    // processed during, e.g., TRACKING_ACTIVE. Non-blocking (handleClient()
    // returns immediately if no request is pending), so it's not a problem
    // for the existing ~20ms loop cadence.
    SetupWebServer::handleClient();

    delay(20);
}

} // namespace StateMachine
