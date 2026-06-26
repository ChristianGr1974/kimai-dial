#include "ui_screens.h"
// SPIFFS.h MUST be included before M5Unified.h/M5GFX.h: M5GFX only enables
// its DataWrapperT<fs::SPIFFSFS> specialization (for loadFont() from
// SPIFFS) if the _SPIFFS_H_ macro is already defined while parsing.
#include <SPIFFS.h>
#include <M5Unified.h>
#include <M5Dial.h>
#include <qrcode.h>
#include "i18n.h"
#include "version.h"

namespace {

constexpr int CENTER_X = 120;
constexpr int CENTER_Y = 120;

// Anti-aliased Helvetica Neue Bold fonts in VLW format (see
// tools/make_vlw.py), stored in SPIFFS under these paths (uploaded via
// `pio run -t uploadfs`). Replace the built-in 1-bit bitmap fonts
// everywhere, which looked pixelated when scaled up. UI_SMALL for
// titles/hints/indicator, UI for prominent texts (project/activity name),
// CLOCK for the large clock display.
constexpr const char *UI_SMALL_FONT_PATH = "/ui_bold_14.vlw";
constexpr const char *UI_FONT_PATH = "/ui_bold_22.vlw";
constexpr const char *CLOCK_FONT_PATH = "/clock_bold_50.vlw";

enum class FontChoice { UI_SMALL, UI, CLOCK };
FontChoice g_currentFont = FontChoice::UI; // forces loading on the very first call
bool g_anyFontLoaded = false;

void useFont(FontChoice choice) {
    if (g_anyFontLoaded && g_currentFont == choice) {
        return; // Avoids unnecessary reloading from SPIFFS on every redraw.
    }
    switch (choice) {
        case FontChoice::UI_SMALL:
            M5Dial.Display.loadFont(SPIFFS, UI_SMALL_FONT_PATH);
            break;
        case FontChoice::UI:
            M5Dial.Display.loadFont(SPIFFS, UI_FONT_PATH);
            break;
        case FontChoice::CLOCK:
            M5Dial.Display.loadFont(SPIFFS, CLOCK_FONT_PATH);
            break;
    }
    g_currentFont = choice;
    g_anyFontLoaded = true;
}

// Current background (project/activity color), so drawCentered() draws
// text with the correct background color instead of hardcoded TFT_BLACK
// (otherwise you'd see bright text boxes with black borders on a colored background).
uint16_t g_bgColor = TFT_BLACK;
uint16_t g_fgColor = TFT_WHITE; // contrast color for text, based on the brightness of g_bgColor

// Parses Kimai hex colors ("#RRGGBB"). Returns false for an empty/invalid string.
bool parseHexColor(const String &hex, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (hex.length() != 7 || hex[0] != '#') {
        return false;
    }
    long value = strtol(hex.c_str() + 1, nullptr, 16);
    r = (value >> 16) & 0xFF;
    g = (value >> 8) & 0xFF;
    b = value & 0xFF;
    return true;
}

// Sets the background and (contrasting) foreground color based on the
// Kimai color. Brightness via the usual luminance formula; black on light,
// white on dark backgrounds.
void setColorScheme(const String &colorHex) {
    uint8_t r, g, b;
    if (colorHex.isEmpty() || !parseHexColor(colorHex, r, g, b)) {
        g_bgColor = TFT_BLACK;
        g_fgColor = TFT_WHITE;
        return;
    }
    g_bgColor = M5Dial.Display.color565(r, g, b);
    int luminance = (r * 299 + g * 587 + b * 114) / 1000;
    g_fgColor = (luminance > 140) ? TFT_BLACK : TFT_WHITE;
}

void resetColorScheme() {
    g_bgColor = TFT_BLACK;
    g_fgColor = TFT_WHITE;
}

void clearScreen() {
    M5Dial.Display.fillScreen(g_bgColor);
}

// Small texts (title line, hints, indicator) use the 14pt variant of the
// same Helvetica Neue Bold font as the prominent texts - the textSize
// parameter is kept, but now scales an already anti-aliased font instead
// of upscaling the old 1-bit bitmap font.
void drawTitle(const char *title) {
    useFont(FontChoice::UI_SMALL);
    M5Dial.Display.setTextDatum(top_center);
    M5Dial.Display.setTextColor(g_fgColor, g_bgColor);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString(title, CENTER_X, 20);
}

void drawCentered(const String &text, int y, int textSize, uint16_t color) {
    useFont(FontChoice::UI_SMALL);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextColor(color, g_bgColor);
    M5Dial.Display.setTextSize(textSize);
    M5Dial.Display.drawString(text, CENTER_X, y);
}

// Overload without a color: uses the contrast color matching the current color scheme.
void drawCentered(const String &text, int y, int textSize) {
    drawCentered(text, y, textSize, g_fgColor);
}

// Prominent texts (project/activity name) in the anti-aliased Helvetica
// Neue Bold font instead of the upscaled bitmap font - that was the
// "pixelated" spot that stood out the most.
void drawCenteredUi(const String &text, int y, uint16_t color) {
    useFont(FontChoice::UI);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextColor(color, g_bgColor);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString(text, CENTER_X, y);
}

void drawCenteredUi(const String &text, int y) {
    drawCenteredUi(text, y, g_fgColor);
}

void drawRetryHint() {
    drawCentered(I18n::t(I18n::Key::RETRY_HINT), 200, 1, TFT_YELLOW);
}

// Draws a QR code (ricmoo/QRCode lib) centered around (cx, topY + half the
// height). ECC level "L" is sufficient for the short WiFi/URL strings here
// and keeps the QR version (= module count) small, which improves
// readability on small displays. Version 0 = automatic version selection
// based on data length in qrcode_initText().
void drawQrCode(const String &text, int cx, int topY, int maxSizePx) {
    // QRCode_t buffer: version 6 is sufficient for well beyond the
    // WiFi QR/URL strings that occur here (up to ~100 bytes at ECC L) and
    // keeps the static buffer small enough for the ESP32-S3 (no PSRAM!).
    constexpr uint8_t QR_VERSION = 6;
    QRCode qrcode;
    uint8_t qrData[qrcode_getBufferSize(QR_VERSION)];
    qrcode_initText(&qrcode, qrData, QR_VERSION, ECC_LOW, text.c_str());

    int modules = qrcode.size;
    int moduleSize = maxSizePx / modules;
    if (moduleSize < 1) {
        moduleSize = 1;
    }
    int totalSize = moduleSize * modules;
    int startX = cx - totalSize / 2;
    int startY = topY;

    // White quiet-zone background for reliable scanning (modules are drawn
    // directly via fillRect() through M5GFX, no image asset).
    M5Dial.Display.fillRect(startX - moduleSize, startY - moduleSize,
                             totalSize + 2 * moduleSize, totalSize + 2 * moduleSize, TFT_WHITE);

    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                M5Dial.Display.fillRect(startX + x * moduleSize, startY + y * moduleSize,
                                         moduleSize, moduleSize, TFT_BLACK);
            }
        }
    }
}

} // namespace

namespace UiScreens {

void renderBoot() {
    resetColorScheme();
    clearScreen();
    drawCenteredUi(I18n::t(I18n::Key::BOOT_TITLE), CENTER_Y - 10);
    drawCentered(I18n::t(I18n::Key::BOOT_STARTING), CENTER_Y + 30, 1);
    drawCentered(String("v") + FIRMWARE_VERSION, 205, 1, TFT_DARKGREY);
}

void renderWifiConnecting() {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::WIFI_CONNECTING_TITLE));
    drawCenteredUi(I18n::t(I18n::Key::WIFI_CONNECTING_TEXT), CENTER_Y);
}

void renderErrorWifi(const String &message) {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::ERROR_WIFI_TITLE));
    drawCentered(message, CENTER_Y, 1, TFT_RED);
    drawRetryHint();
}

void renderLoadingData() {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::LOADING_DATA_TITLE));
    drawCenteredUi(I18n::t(I18n::Key::LOADING_DATA_TEXT), CENTER_Y);
}

void renderErrorApi(const String &message) {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::ERROR_API_TITLE));
    drawCentered(message, CENTER_Y, 1, TFT_RED);
    drawRetryHint();
}

// Project/activity browse are now carousel screens: these functions only
// draw the title + hint line (no more fixed background from the
// project/activity color - the color now lives only inside the respective
// carousel circle). main.cpp additionally calls carousel.render().
// Draws ONLY the empty case directly to the display (no carousel present).
// In the normal case (projects/activities present) this function does
// nothing anymore - title + list come entirely from the carousel sprite
// (see Carousel::setTitle()), which now covers the ENTIRE display.
// Previously, the title+hint text was additionally drawn directly to the
// display here, which was immediately overwritten by the sprite in the
// same tick - pure duplicate work that had also reintroduced a brief
// flicker/artifact risk.
void renderProjectBrowse(const AppContext &ctx) {
    if (!ctx.selection.projects.empty()) {
        return;
    }
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::PROJECT_TITLE));
    drawCentered(I18n::t(I18n::Key::PROJECT_EMPTY), CENTER_Y, 1);
}

void renderActivityBrowse(const AppContext &ctx) {
    if (!ctx.selection.activities.empty()) {
        return;
    }
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::ACTIVITY_TITLE));
    drawCentered(I18n::t(I18n::Key::ACTIVITY_EMPTY), CENTER_Y, 1);
    drawRetryHint();
}

void renderStartingEntry() {
    clearScreen();
    drawTitle(I18n::t(I18n::Key::LOADING_DATA_TITLE));
    drawCenteredUi(I18n::t(I18n::Key::STARTING_ENTRY), CENTER_Y);
}

namespace {
// Area of the clock display, for the partial redraw in updateTrackingClock().
// The clock font (50pt Helvetica Neue Bold) is noticeably taller than the
// old upscaled bitmap font, hence a more generous rect.
constexpr int CLOCK_RECT_X = 10;
constexpr int CLOCK_RECT_Y = CENTER_Y - 50;
constexpr int CLOCK_RECT_W = 220;
constexpr int CLOCK_RECT_H = 80;
} // namespace

void renderTrackingActive(const AppContext &ctx, unsigned long elapsedSeconds) {
    setColorScheme(ctx.tracking.activeColorHex);
    clearScreen();

    if (!ctx.tracking.activeProjectName.isEmpty()) {
        drawCenteredUi(ctx.tracking.activeProjectName, CENTER_Y + 45);
    }
    if (!ctx.tracking.activeActivityName.isEmpty()) {
        drawCentered(ctx.tracking.activeActivityName, CENTER_Y + 65, 1);
    }

    if (ctx.offlineNotice) {
        drawCentered(I18n::t(I18n::Key::TRACKING_OFFLINE), 215, 1, TFT_ORANGE);
    }

    updateTrackingClock(elapsedSeconds);
}

void updateTrackingClock(unsigned long elapsedSeconds) {
    useFont(FontChoice::CLOCK);
    // Clear only the clock area instead of the whole screen - avoids the
    // visible flash (fillScreen black -> redraw everything) on every second change.
    // Use the background color of the current color scheme, not hardcoded
    // black, otherwise a black box appears on a colored tracking background.
    M5Dial.Display.fillRect(CLOCK_RECT_X, CLOCK_RECT_Y, CLOCK_RECT_W, CLOCK_RECT_H, g_bgColor);

    unsigned long h = elapsedSeconds / 3600;
    unsigned long m = (elapsedSeconds % 3600) / 60;
    unsigned long s = elapsedSeconds % 60;
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu", h, m, s);

    // The clock font is already active here (set by this function's
    // caller) - draw directly instead of via drawCentered(), which would
    // switch to DEFAULT_BITMAP.
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextColor(g_fgColor, g_bgColor);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString(timeBuf, CENTER_X, CENTER_Y - 10);
}

void renderStoppingEntry() {
    clearScreen();
    drawTitle(I18n::t(I18n::Key::LOADING_DATA_TITLE));
    drawCenteredUi(I18n::t(I18n::Key::STOPPING_ENTRY), CENTER_Y);
}

void renderWifiScanListFrame() {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::WIFI_CONNECTING_TITLE));
    drawCentered(I18n::t(I18n::Key::PROJECT_HINT), 215, 1);
}

void renderWifiScanning() {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::WIFI_CONNECTING_TITLE));
    drawCenteredUi(I18n::t(I18n::Key::LOADING_DATA_TEXT), CENTER_Y);
}

void renderWifiNoNetworksFound() {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::WIFI_CONNECTING_TITLE));
    drawCentered(I18n::t(I18n::Key::PROJECT_EMPTY), CENTER_Y, 1);
    drawCentered(I18n::t(I18n::Key::RETRY_HINT), 215, 1);
}

// Deliberately ONLY the QR code, without text details (SSID/password/user
// etc.) - the detailed overview is now available separately under "Status"
// (renderSettingsStatus()). Setup on the device should be a quick, clear
// "grab your phone and scan" screen.
void renderSetupAp(const String &apSsid, const String &apPassword) {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::SETUP_WLAN_TITLE));

    // WiFi QR format: scanning with the phone camera automatically
    // connects to the setup AP WiFi. AP_PASSWORD is never empty (see
    // setup_webserver.cpp), hence always "WPA" instead of "nopass" here.
    String wifiQr = "WIFI:T:WPA;S:" + apSsid + ";P:" + apPassword + ";;";
    drawQrCode(wifiQr, CENTER_X, 40, 140);

    // After connecting to WiFi, most phones automatically show a "sign in
    // to network" prompt leading to the settings page (captive portal DNS
    // redirect, see setup_webserver.cpp). If not (some smartphones
    // suppress that), the fixed AP gateway IP is the fallback path -
    // hence also shown here as text.
    drawCentered(I18n::t(I18n::Key::SETUP_NO_LOGIN_PAGE), 188, 1, TFT_DARKGREY);
    drawCentered("192.168.4.1", 203, 1, TFT_YELLOW);
}

void renderSetupLanInfo(const String &ipAddress) {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::SETUP_URL_TITLE));

    String url = "http://" + ipAddress + "/";
    drawQrCode(url, CENTER_X, CENTER_Y - 80, 160);
}

// Text overview (no QR code): user, current WiFi SSID, settings URL (if
// connected to the LAN), and API token (shown truncated, never in full for
// security reasons - per the architecture spec, analogous to the password
// field on the web settings page). connectedSsid/url empty = not connected
// to the LAN (e.g. still in AP setup mode).
void renderSettingsStatus(const String &kimaiUser, const String &wifiSsid, const String &url,
                           const String &apiToken) {
    resetColorScheme();
    clearScreen();
    drawTitle(I18n::t(I18n::Key::STATUS_TITLE));

    // Round display: only an inscribed rectangle (roughly y 45..205) is
    // really readable edge-to-edge; outside of that, the bezel cuts off
    // wide lines (per photo feedback). That's why the small 14pt font
    // (drawCentered) is used throughout here instead of drawCenteredUi
    // (22pt) - with four label+value lines, this is the only variant that
    // reliably fits both in height and width (long URLs/tokens).
    int y = 44;
    constexpr int LINE_H = 38;

    // "http://" and the trailing slash are dropped - only the IP is the
    // actual information, the prefix just costs width.
    String urlShown = url;
    urlShown.replace("http://", "");
    if (urlShown.endsWith("/")) {
        urlShown.remove(urlShown.length() - 1);
    }

    // Values shown a bit larger (22pt UI font instead of 14pt) - with four
    // short lines this still fits safely within the readable area, while
    // making the most important information (the actual values) easier to
    // read. The label stays small (14pt) so the label/value contrast is preserved.
    drawCentered(I18n::t(I18n::Key::STATUS_USER), y, 1, TFT_DARKGREY);
    drawCenteredUi(kimaiUser.isEmpty() ? I18n::t(I18n::Key::STATUS_NOT_SET) : kimaiUser, y + 20);
    y += LINE_H;

    drawCentered(I18n::t(I18n::Key::STATUS_WLAN), y, 1, TFT_DARKGREY);
    drawCenteredUi(wifiSsid.isEmpty() ? I18n::t(I18n::Key::STATUS_NOT_SET) : wifiSsid, y + 20);
    y += LINE_H;

    drawCentered(I18n::t(I18n::Key::STATUS_URL), y, 1, TFT_DARKGREY);
    drawCenteredUi(urlShown.isEmpty() ? I18n::t(I18n::Key::STATUS_NOT_ON_LAN) : urlShown, y + 20);
    y += LINE_H;

    // Token only hinted at (first 6 characters + "..."), showing it in
    // full would be a security risk if someone is looking over your shoulder.
    String tokenShown = I18n::t(I18n::Key::STATUS_NOT_SET);
    if (!apiToken.isEmpty()) {
        tokenShown = apiToken.length() > 6 ? apiToken.substring(0, 6) + "..." : apiToken;
    }
    drawCentered(I18n::t(I18n::Key::STATUS_TOKEN), y, 1, TFT_DARKGREY);
    drawCenteredUi(tokenShown, y + 20);
}

} // namespace UiScreens
