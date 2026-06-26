#pragma once

#include <Arduino.h>
#include <vector>

// Main state machine.
//
// Note on ACTIVITY_BROWSE: the architecture spec left open whether the
// activity browse stage should be its own state or a flag inside
// LIST_BROWSE. We use a dedicated state (ACTIVITY_BROWSE) so render()/update()
// stay cleanly separated per stage and no extra flag needs to be threaded
// through the whole codebase.
enum class AppState {
    BOOT,
    WIFI_CONNECTING,
    ERROR_WIFI,
    LOADING_DATA,
    ERROR_API,
    LIST_BROWSE,      // Project selection
    ACTIVITY_BROWSE,  // Activity selection within the chosen project
    STARTING_ENTRY,
    TRACKING_ACTIVE,
    STOPPING_ENTRY,
    // New states for carousel navigation (settings/onboarding):
    MAIN_MENU,
    SETTINGS_MENU,
    // Setup via AP+browser replaces the former rotary character picker
    // (char_picker.h/.cpp, removed) entirely - see setup_webserver.h/.cpp.
    SETUP_AP,        // Device is not on the LAN: AP active, shows only the QR code
    SETUP_LAN_INFO,  // Device is on the LAN: shows only the QR code for the settings page
    SETTINGS_STATUS  // Text overview: user, WiFi SSID, URL, API token
};

struct KimaiProject {
    int id = -1;
    String name;
    String customerTitle; // parentTitle from the API, for display
    String colorHex;      // "color" from the API, e.g. "#808000"; empty = no color set
};

struct KimaiActivity {
    int id = -1;
    int projectId = -1;
    String name;
    String colorHex;
};

// What last failed, so retry can repeat the right step.
enum class LastAction {
    NONE,
    CONNECT_WIFI,
    LOAD_PROJECTS,
    LOAD_ACTIVITIES,
    START_TIMESHEET,
    STOP_TIMESHEET
};

// Project/activity browse state: loaded lists plus the current
// encoder selection position within them.
struct BrowseSelection {
    std::vector<KimaiProject> projects;
    std::vector<KimaiActivity> activities;
    int selectedProjectIndex = 0;
    int selectedActivityIndex = 0;
    // On resume (set before the project list is loaded), we track whether
    // a long-press from LIST_BROWSE is allowed to go back to the main menu -
    // currently always true, field kept for clarity in the code.
    bool projectsLoaded = false;
};

// Currently running (or most recently started) timesheet entry.
struct TrackingSession {
    int activeTimesheetId = -1;
    unsigned long trackingStartMillis = 0; // for the running clock, NOT API-based
    String activeProjectName;
    String activeActivityName;
    String activeColorHex; // color of the activity (or project as fallback), for the tracking background
};

// Last error, so retry can repeat the right step.
struct ErrorContext {
    String errorMessage;
    LastAction lastFailedAction = LastAction::NONE;
};

struct AppContext {
    AppState state = AppState::BOOT;
    BrowseSelection selection;
    TrackingSession tracking;
    ErrorContext error;

    bool wifiConnected = false;
    bool offlineNotice = false; // for the small hint shown during TRACKING_ACTIVE
};
