#include "setup_webserver.h"
#include <WebServer.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "settings_store.h"
#include "wifi_manager.h"
#include "i18n.h"
#include "version.h"

namespace {

WebServer server(80);
DNSServer dnsServer;
bool running = false;

// Small local helper instead of a full second i18n table for HTML: picks
// the DE/EN label based on the SAME language setting as the device display
// (SettingsStore::getLanguage()) - one shared setting, no separate language
// switch in the browser.
const char *label(const char *de, const char *en) {
    return (SettingsStore::getLanguage() == Language::DE) ? de : en;
}
// A curated list of POSIX TZ strings instead of a free-text field - covers
// the common cases (including correct DST rules per region) without
// needing the full IANA timezone database on the device. Add more entries
// here if needed; the value itself is passed straight to configTzTime().
struct TimezoneOption {
    const char *posixTz;
    const char *label;
};
constexpr TimezoneOption TIMEZONE_OPTIONS[] = {
    {"CET-1CEST,M3.5.0,M10.5.0/3", "Europe/Berlin (CET/CEST)"},
    {"GMT0BST,M3.5.0/1,M10.5.0", "Europe/London (GMT/BST)"},
    {"EET-2EEST,M3.5.0/3,M10.5.0/4", "Europe/Helsinki (EET/EEST)"},
    {"MSK-3", "Europe/Moscow (MSK)"},
    {"UTC0", "UTC"},
    {"EST5EDT,M3.2.0,M11.1.0", "US Eastern (EST/EDT)"},
    {"CST6CDT,M3.2.0,M11.1.0", "US Central (CST/CDT)"},
    {"MST7MDT,M3.2.0,M11.1.0", "US Mountain (MST/MDT)"},
    {"PST8PDT,M3.2.0,M11.1.0", "US Pacific (PST/PDT)"},
    {"JST-9", "Asia/Tokyo (JST)"},
    {"AEST-10AEDT,M10.1.0,M4.1.0/3", "Australia/Sydney (AEST/AEDT)"},
};
constexpr size_t TIMEZONE_OPTIONS_COUNT = sizeof(TIMEZONE_OPTIONS) / sizeof(TIMEZONE_OPTIONS[0]);

// Separate flag instead of coupling to "running": the DNS server (captive
// portal redirect, see startAccessPoint()) must run ONLY while the AP is
// active - in pure STA web server mode (startInStationMode()) there is no
// AP and therefore no captive portal requests to redirect.
bool apActive = false;

// Minimal inline HTML, no framework - readable on a phone screen.
// Password/token fields are NEVER pre-filled with the current value
// (security reason); only SSID/user are pre-filled.
//
// WiFi and Kimai credentials are deliberately TWO separate <form>s with
// their own endpoints (/save-wifi, /save-kimai) instead of one combined
// submit: this way you can, e.g., renew only the API token without
// accidentally also submitting an empty WiFi password field (which would
// otherwise be interpreted as "unchanged", but with separate forms isn't
// even part of the Kimai submit to begin with). Reset is a clearly
// separated third section.
//
// In AP mode (device not yet on the LAN), the page shows ONLY the WiFi
// form: the AP is isolated, with no route to the actual Kimai server - the
// Kimai form only appears once the page is accessed on the LAN
// (startInStationMode(), after a successful WiFi connect).
String buildSettingsPage(bool apMode) {
    String html;
    html.reserve(2560);
    html += F(
        "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Kimai Dial Setup</title><style>"
        "body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 16px;background:#1a1a1a;color:#eee}"
        "h1{font-size:20px}h2{font-size:16px;margin-top:32px;border-top:1px solid #333;padding-top:16px}"
        "label{display:block;margin-top:16px;font-size:14px;color:#aaa}"
        "input,select{width:100%;box-sizing:border-box;padding:10px;font-size:16px;margin-top:4px;"
        "border:1px solid #444;border-radius:6px;background:#2a2a2a;color:#fff}"
        "small{color:#888}"
        "button{margin-top:16px;width:100%;padding:12px;font-size:16px;background:#2E86DE;color:#fff;"
        "border:none;border-radius:6px}"
        "</style></head><body><h1>Kimai Dial Setup</h1>");

    html += "<h2>" + String(label("WLAN", "WiFi")) + "</h2><form method='POST' action='/save-wifi'>";
    html += "<label>" + String(label("WLAN-SSID", "WiFi SSID")) + "</label><input name='ssid' value='";
    html += SettingsStore::getWifiSsid();
    html += F("'>");
    html += "<label>" + String(label("WLAN-Passwort", "WiFi password")) + " <small>(" +
            String(label("leer lassen = unverändert", "leave blank = unchanged")) +
            ")</small></label>"
            "<input type='password' name='wifi_pass' placeholder='" +
            String(label("unverändert", "unchanged")) + "'>";
    html += "<button type='submit'>" + String(label("WLAN speichern &amp; Neustart", "Save WiFi &amp; restart")) +
            "</button></form>";

    if (apMode) {
        html += "<p style='color:#aaa'>" +
                String(label("Kimai-Server-Adresse/Benutzer/Token gibt es auf der "
                              "Einstellungsseite im Netzwerk, sobald das Gerät verbunden ist (siehe Display).",
                              "The Kimai server URL/user/token are available on the settings page on "
                              "the LAN once the device is connected (see display).")) +
                "</p>";
    } else {
        html += "<h2>Kimai</h2><form method='POST' action='/save-kimai'>";
        html += "<label>" + String(label("Kimai-Server-Adresse", "Kimai server URL")) +
                " <small>z.B. http://192.168.0.143:8001</small></label>"
                "<input name='base_url' value='";
        html += SettingsStore::getKimaiBaseUrl();
        html += F("'>");
        html += "<label>" + String(label("Kimai-Benutzername", "Kimai username")) + "</label><input name='user' value='";
        html += SettingsStore::getKimaiUser();
        html += F("'>");
        html += "<label>" + String(label("Kimai-API-Token", "Kimai API token")) + " <small>(" +
                String(label("leer lassen = unverändert", "leave blank = unchanged")) +
                ")</small></label>"
                "<input type='password' name='token' placeholder='" +
                String(label("unverändert", "unchanged")) + "'>";
        html += "<button type='submit'>" + String(label("Kimai speichern &amp; Neustart", "Save Kimai &amp; restart")) +
                "</button></form>";
    }

    html += "<h2>" + String(label("Zeitzone", "Timezone")) + "</h2><form method='POST' action='/save-timezone'>";
    html += "<label>" + String(label("Zeitzone", "Timezone")) + "</label><select name='timezone'>";
    String currentTz = SettingsStore::getTimezone();
    for (size_t i = 0; i < TIMEZONE_OPTIONS_COUNT; i++) {
        html += "<option value='" + String(TIMEZONE_OPTIONS[i].posixTz) + "'";
        if (currentTz == TIMEZONE_OPTIONS[i].posixTz) {
            html += " selected";
        }
        html += ">" + String(TIMEZONE_OPTIONS[i].label) + "</option>";
    }
    html += "</select>";
    html += "<button type='submit'>" + String(label("Zeitzone speichern", "Save timezone")) +
            "</button></form>";

    html += "<h2>" + String(label("Zurücksetzen", "Reset")) +
            "</h2>"
            "<form method='POST' action='/reset' onsubmit=\"return confirm('" +
            String(label("Wirklich ALLES zurücksetzen (WLAN + Kimai-Zugangsdaten)?",
                          "Really reset EVERYTHING (WiFi + Kimai credentials)?")) +
            "');\">"
            "<button type='submit' style='background:#c0392b'>" +
            String(label("Werksreset (alles vergessen)", "Factory reset (forget everything)")) +
            "</button></form>"
            "<p style='color:#555;font-size:12px;text-align:center;margin-top:32px'>Kimai Dial v" +
            String(FIRMWARE_VERSION) + "</p>"
            "</body></html>";
    return html;
}

String buildResetPage() {
    String html;
    html += F("<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Reset</title><style>body{font-family:sans-serif;text-align:center;margin-top:80px;"
               "background:#1a1a1a;color:#eee}</style></head><body>");
    html += "<h1>" + String(label("Zurückgesetzt", "Reset complete")) + "</h1><p>" +
            String(label("Gerät startet neu in den Einrichtungsmodus...", "Device is restarting into setup mode...")) +
            "</p></body></html>";
    return html;
}

String buildSavedPage() {
    String html;
    html += F("<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Gespeichert</title><style>body{font-family:sans-serif;text-align:center;margin-top:80px;"
               "background:#1a1a1a;color:#eee}</style></head><body>");
    html += "<h1>" + String(label("Gespeichert", "Saved")) + "</h1><p>" +
            String(label("Gerät startet neu...", "Device is restarting...")) + "</p></body></html>";
    return html;
}

void handleRoot() {
    server.send(200, "text/html", buildSettingsPage(apActive));
}

void handleSaveWifi() {
    // Only take over non-empty fields, otherwise leaving the password
    // field blank would accidentally delete the stored value.
    String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
    String wifiPass = server.hasArg("wifi_pass") ? server.arg("wifi_pass") : "";

    if (!ssid.isEmpty()) {
        String passToSave = wifiPass.isEmpty() ? SettingsStore::getWifiPassword() : wifiPass;
        SettingsStore::saveWifi(ssid, passToSave);
    } else if (!wifiPass.isEmpty()) {
        SettingsStore::saveWifi(SettingsStore::getWifiSsid(), wifiPass);
    }

    server.send(200, "text/html", buildSavedPage());
    // Pragmatic: a blocking delay() right in the request handler, since
    // this is a one-off operation (per the architecture spec) - the
    // confirmation page has already gone out via server.send() at this point.
    delay(1500);
    ESP.restart();
}

void handleSaveKimai() {
    String baseUrl = server.hasArg("base_url") ? server.arg("base_url") : "";
    String user = server.hasArg("user") ? server.arg("user") : "";
    String token = server.hasArg("token") ? server.arg("token") : "";

    if (!baseUrl.isEmpty()) {
        // Strip a trailing slash, since kimai_client.cpp appends the path
        // ("/api/...") directly - otherwise the URL would end up with a double slash.
        if (baseUrl.endsWith("/")) {
            baseUrl.remove(baseUrl.length() - 1);
        }
        SettingsStore::saveBaseUrl(baseUrl);
    }

    if (!user.isEmpty() || !token.isEmpty()) {
        String userToSave = user.isEmpty() ? SettingsStore::getKimaiUser() : user;
        String tokenToSave = token.isEmpty() ? SettingsStore::getKimaiToken() : token;
        SettingsStore::saveUser(userToSave, tokenToSave);
    }

    server.send(200, "text/html", buildSavedPage());
    delay(1500);
    ESP.restart();
}

void handleSaveTimezone() {
    String tz = server.hasArg("timezone") ? server.arg("timezone") : "";
    if (!tz.isEmpty()) {
        SettingsStore::saveTimezone(tz);
        // No reboot needed here (unlike WiFi/Kimai saves): just re-apply
        // the new TZ to the already-running NTP sync if we're connected.
        if (WiFi.status() == WL_CONNECTED) {
            WifiManager::startNtpSync(tz);
        }
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void handleReset() {
    SettingsStore::factoryReset();
    server.send(200, "text/html", buildResetPage());
    delay(1500);
    ESP.restart();
}

void registerRoutes() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save-wifi", HTTP_POST, handleSaveWifi);
    server.on("/save-kimai", HTTP_POST, handleSaveKimai);
    server.on("/save-timezone", HTTP_POST, handleSaveTimezone);
    server.on("/reset", HTTP_POST, handleReset);
    server.onNotFound([]() {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
    });
}

} // namespace

namespace SetupWebServer {

void startAccessPoint() {
    WiFi.mode(WIFI_AP_STA); // AP + still allows STA connection attempts in the background
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    if (!running) {
        registerRoutes();
        server.begin();
        running = true;
    }
    // DNS catch-all (every request -> AP IP): this is the actual captive
    // portal trick. After connecting to WiFi, smartphones automatically
    // check a known test address (e.g. connectivitycheck.android.com);
    // since the DNS answer here ALWAYS points to the AP IP, they get the
    // settings page instead of the expected response and show their own
    // "sign in to network" prompt that leads directly to the settings page
    // - the user no longer has to type in the IP/URL themselves.
    dnsServer.start(53, "*", WiFi.softAPIP());
    apActive = true;
}

void startInStationMode() {
    if (!running) {
        registerRoutes();
        server.begin();
        running = true;
    }
}

void stopAccessPoint() {
    dnsServer.stop();
    apActive = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

void handleClient() {
    if (apActive) {
        dnsServer.processNextRequest();
    }
    if (running) {
        server.handleClient();
    }
}

bool isRunning() {
    return running;
}

} // namespace SetupWebServer
