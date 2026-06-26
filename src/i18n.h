#pragma once
#include <Arduino.h>

// Lightweight, allocation-free i18n module for the device UI.
// Two languages only (DE/EN), selected by a single global setting.
enum class Language : uint8_t { DE = 0, EN = 1 };

namespace I18n {

void setLanguage(Language lang);
Language currentLanguage();

// Enum keys instead of raw strings - typo-safe, IDE-autocomplete friendly.
enum class Key {
    MAIN_MENU_TITLE,
    MAIN_MENU_TRACKING,
    MAIN_MENU_SETTINGS,
    SETTINGS_MENU_TITLE,
    SETTINGS_SETUP,
    SETTINGS_STATUS,
    SETTINGS_LANGUAGE,
    BACK,
    PROJECT_TITLE,
    PROJECT_EMPTY,
    PROJECT_HINT,
    ACTIVITY_TITLE,
    ACTIVITY_EMPTY,
    ACTIVITY_HINT,
    STARTING_ENTRY,
    STOPPING_ENTRY,
    TRACKING_OFFLINE,
    BOOT_TITLE,
    BOOT_STARTING,
    WIFI_CONNECTING_TITLE,
    WIFI_CONNECTING_TEXT,
    ERROR_WIFI_TITLE,
    ERROR_API_TITLE,
    RETRY_HINT,
    LOADING_DATA_TITLE,
    LOADING_DATA_TEXT,
    SETUP_WLAN_TITLE,
    SETUP_URL_TITLE,
    SETUP_NO_LOGIN_PAGE,
    STATUS_TITLE,
    STATUS_USER,
    STATUS_WLAN,
    STATUS_URL,
    STATUS_TOKEN,
    STATUS_NOT_SET,
    STATUS_NOT_ON_LAN,
    LANGUAGE_GERMAN,
    LANGUAGE_ENGLISH,
    COUNT // sentinel, must stay last - used only for static_assert sizing
};

// Returns the string for `key` in the currently configured language.
const char *t(Key key);

} // namespace I18n
