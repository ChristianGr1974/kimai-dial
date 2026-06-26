#include "i18n.h"

namespace {

Language g_currentLanguage = Language::DE;

// IMPORTANT: both tables must have EXACTLY as many entries as the Key enum
// (excluding the COUNT sentinel), in the SAME order. The static_assert below
// only catches a count mismatch, not a wrong order - double-check both
// tables stay aligned whenever a key is added/removed/reordered.
constexpr size_t KEY_COUNT = static_cast<size_t>(I18n::Key::COUNT);

static const char *const TABLE_DE[KEY_COUNT] = {
    "Kimai Dial",                     // MAIN_MENU_TITLE
    "Zeiterfassung",                  // MAIN_MENU_TRACKING
    "Settings",                       // MAIN_MENU_SETTINGS
    "Settings",                       // SETTINGS_MENU_TITLE
    "Setup",                          // SETTINGS_SETUP
    "Status",                        // SETTINGS_STATUS
    "Sprache",                        // SETTINGS_LANGUAGE
    "Zurueck",                        // BACK
    "Projekt",                        // PROJECT_TITLE
    "Keine Projekte",                 // PROJECT_EMPTY
    "Klick = waehlen, Lang = zurueck",// PROJECT_HINT
    "Aktivitaet",                     // ACTIVITY_TITLE
    "Keine Aktivitaeten",             // ACTIVITY_EMPTY
    "Klick = waehlen, Lang = zurueck",// ACTIVITY_HINT
    "Starte Timer...",                // STARTING_ENTRY
    "Stoppe Timer...",                // STOPPING_ENTRY
    "offline",                        // TRACKING_OFFLINE
    "Kimai Dial",                     // BOOT_TITLE
    "Starte...",                      // BOOT_STARTING
    "WLAN",                           // WIFI_CONNECTING_TITLE
    "Connecting WiFi...",             // WIFI_CONNECTING_TEXT
    "WLAN Fehler",                    // ERROR_WIFI_TITLE
    "API Fehler",                     // ERROR_API_TITLE
    "Klick fuer Retry",               // RETRY_HINT
    "Kimai",                          // LOADING_DATA_TITLE
    "Lade Daten...",                  // LOADING_DATA_TEXT
    "Setup-WLAN",                     // SETUP_WLAN_TITLE
    "Settings-URL",                   // SETUP_URL_TITLE
    "Falls keine Anmeldeseite:",      // SETUP_NO_LOGIN_PAGE
    "Status",                         // STATUS_TITLE
    "User",                           // STATUS_USER
    "WLAN",                           // STATUS_WLAN
    "URL",                            // STATUS_URL
    "API-Token",                      // STATUS_TOKEN
    "(nicht gesetzt)",                // STATUS_NOT_SET
    "(nicht im LAN)",                 // STATUS_NOT_ON_LAN
    "Deutsch",                        // LANGUAGE_GERMAN
    "English",                        // LANGUAGE_ENGLISH
};

static const char *const TABLE_EN[KEY_COUNT] = {
    "Kimai Dial",                     // MAIN_MENU_TITLE
    "Tracking",                       // MAIN_MENU_TRACKING
    "Settings",                       // MAIN_MENU_SETTINGS
    "Settings",                       // SETTINGS_MENU_TITLE
    "Setup",                          // SETTINGS_SETUP
    "Status",                        // SETTINGS_STATUS
    "Language",                       // SETTINGS_LANGUAGE
    "Back",                           // BACK
    "Project",                        // PROJECT_TITLE
    "No projects",                    // PROJECT_EMPTY
    "Click = select, Hold = back",    // PROJECT_HINT
    "Activity",                       // ACTIVITY_TITLE
    "No activities",                  // ACTIVITY_EMPTY
    "Click = select, Hold = back",    // ACTIVITY_HINT
    "Starting timer...",              // STARTING_ENTRY
    "Stopping timer...",              // STOPPING_ENTRY
    "offline",                        // TRACKING_OFFLINE
    "Kimai Dial",                     // BOOT_TITLE
    "Starting...",                    // BOOT_STARTING
    "WiFi",                           // WIFI_CONNECTING_TITLE
    "Connecting WiFi...",             // WIFI_CONNECTING_TEXT
    "WiFi Error",                     // ERROR_WIFI_TITLE
    "API Error",                      // ERROR_API_TITLE
    "Click to retry",                 // RETRY_HINT
    "Kimai",                          // LOADING_DATA_TITLE
    "Loading data...",                // LOADING_DATA_TEXT
    "Setup WiFi",                     // SETUP_WLAN_TITLE
    "Settings URL",                   // SETUP_URL_TITLE
    "If no login page appears:",      // SETUP_NO_LOGIN_PAGE
    "Status",                         // STATUS_TITLE
    "User",                           // STATUS_USER
    "WiFi",                           // STATUS_WLAN
    "URL",                            // STATUS_URL
    "API token",                      // STATUS_TOKEN
    "(not set)",                      // STATUS_NOT_SET
    "(not on LAN)",                   // STATUS_NOT_ON_LAN
    "German",                         // LANGUAGE_GERMAN
    "English",                        // LANGUAGE_ENGLISH
};

} // namespace

namespace I18n {

void setLanguage(Language lang) {
    g_currentLanguage = lang;
}

Language currentLanguage() {
    return g_currentLanguage;
}

const char *t(Key key) {
    size_t index = static_cast<size_t>(key);
    if (index >= KEY_COUNT) {
        return "";
    }
    return (g_currentLanguage == Language::DE) ? TABLE_DE[index] : TABLE_EN[index];
}

} // namespace I18n
