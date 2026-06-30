#pragma once

#include <Arduino.h>
#include <vector>

enum class AppState {
    BOOT,
    WIFI_CONNECTING,
    ERROR_WIFI,
    LOADING_DATA,
    ERROR_API,
    CUSTOMER_BROWSE,  // Customer selection (first step of time tracking)
    LIST_BROWSE,      // Project selection (filtered by selected customer)
    ACTIVITY_BROWSE,  // Activity selection within the chosen project
    TRACKING_SESSION, // Start/pause/stop screen before and during tracking
    STARTING_ENTRY,
    TRACKING_ACTIVE,
    STOPPING_ENTRY,
    MAIN_MENU,
    SETTINGS_MENU,
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
    std::vector<KimaiProject> projects;          // all projects (full list, for customer extraction)
    std::vector<KimaiProject> filteredProjects;  // projects filtered by the selected customer
    std::vector<KimaiActivity> activities;
    std::vector<String> customers;               // unique customer names, derived from projects
    int selectedCustomerIndex = 0;
    int selectedProjectIndex = 0;                // indexes into filteredProjects
    int selectedActivityIndex = 0;
    bool projectsLoaded = false;
};

// Currently running (or most recently started) timesheet entry.
struct TrackingSession {
    int activeTimesheetId = -1;
    unsigned long trackingStartMillis = 0; // millis() when the current run started
    unsigned long accumulatedMs = 0;       // total ms from all previous start/pause cycles
    int projectId = -1;                    // needed to start a new timesheet after resume
    int activityId = -1;
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
