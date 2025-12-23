/*
BaseUI

Developed and Maintained By:
- Ronald Garcia (HarukiToreda) – Lead development and implementation.
- JasonP (Xaositek)  – Screen layout and icon design, UI improvements and testing.
- TonyG (Tropho) – Project management, structural planning, and testing

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "Throttle.h"
#include "configuration.h"
#include "meshUtils.h"
#include "audio/ToneOutput.h"
#include "batt/BattMeterClient.h"
#include "graphics/SecretMenuImage.h"
#include "graphics/MsdCalculatorPage.h"
#include "marauder/StationTracker.h"
#include "marauder/WifiAttackController.h"
#include "ir/TVBGone.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "DisplayFormatters.h"
#include "TimeFormatters.h"
#include "draw/ClockRenderer.h"
#include "draw/DebugRenderer.h"
#include "draw/MenuHandler.h"
#include "draw/MessageRenderer.h"
#include "draw/NodeListRenderer.h"
#include "draw/NotificationRenderer.h"
#include "draw/UIRenderer.h"
#include "modules/CannedMessageModule.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#include "buzz.h"
#endif
#include "FSCommon.h"
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "error.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/emotes.h"
#include "graphics/images.h"
#include "input/TouchScreenImpl1.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "modules/WaypointModule.h"
#include "sleep.h"
#include "target_specific.h"

using graphics::Emote;
using graphics::emotes;
using graphics::numEmotes;

#if USE_TFTDISPLAY
extern uint16_t TFT_MESH;
#else
uint16_t TFT_MESH = COLOR565(0xFF, 0x00, 0x00);
#endif

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#endif
#if HAS_WIFI
#include <WiFi.h>
#endif

#ifdef ARCH_ESP32
#include "driver/i2s.h"
#endif

#if ARCH_PORTDUINO
#include "modules/StoreForwardModule.h"
#include "platform/portduino/PortduinoGlue.h"
#endif

#if defined(T_LORA_PAGER)
// KB backlight control
#include "input/cardKbI2cImpl.h"
#endif

using namespace meshtastic; /** @todo remove */

namespace graphics
{

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define IDLE_FRAMERATE 1 // in fps

// DEBUG
#define NUM_EXTRA_FRAMES 3 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS
#define ASCII_BELL '\x07'
// A text message frame + debug frame + all the node infos
FrameCallback *normalFrames;
static uint32_t targetFramerate = IDLE_FRAMERATE;
// Global variables for alert banner - explicitly define with extern "C" linkage to prevent optimization

uint32_t logo_timeout = 5000; // 4 seconds for EACH logo

// Threshold values for the GPS lock accuracy bar display
uint32_t dopThresholds[5] = {2000, 1000, 500, 200, 100};

// At some point, we're going to ask all of the modules if they would like to display a screen frame
// we'll need to hold onto pointers for the modules that can draw a frame.
std::vector<MeshModule *> moduleFrames;

// Global variables for screen function overlay symbols
std::vector<std::string> functionSymbol;
std::string functionSymbolString;

namespace
{
enum struct SecretMenuEntry : size_t {
    TvBGone,
    MsdCalculator,
    PactTimer,
    Detonate,
    BatteryMeter,
    WifiAttacks,
    WifiScanner,
    StationBrowser,
    ToneGenerator,
    DbMeter,
    Count
};

// Order the visible tools explicitly to match the on-device carousel.
constexpr std::array<SecretMenuEntry, static_cast<size_t>(SecretMenuEntry::Count)> secretMenuOrder = {
    SecretMenuEntry::TvBGone,      SecretMenuEntry::MsdCalculator, SecretMenuEntry::PactTimer,   SecretMenuEntry::Detonate,
    SecretMenuEntry::BatteryMeter, SecretMenuEntry::WifiAttacks,   SecretMenuEntry::WifiScanner, SecretMenuEntry::StationBrowser,
    SecretMenuEntry::ToneGenerator, SecretMenuEntry::DbMeter};

constexpr std::array<const char *, static_cast<size_t>(SecretMenuEntry::Count)> secretMenuItemNames = {
    "TV B Gone",       "MSD Calculator", "PACT Timer",     "Detonate",      "Battery Meter",
    "WiFi Attacks",    "WiFi Scanner",   "Station Browser","Tone Generator","DB Meter"};
constexpr size_t secretMenuItemCount = secretMenuOrder.size();

constexpr size_t wifiAttackItemCount = marauder::kWifiAttackItemCount;
constexpr size_t wifiAttackVisibleCount = 6;

static SecretMenuEntry selectedSecretMenuEntry(size_t selection)
{
    return secretMenuOrder[selection % secretMenuItemCount];
}

static const char *secretMenuItemName(SecretMenuEntry entry)
{
    return secretMenuItemNames[static_cast<size_t>(entry)];
}

static std::string secretMenuItemLabel(size_t index)
{
    SecretMenuEntry entry = selectedSecretMenuEntry(index);
    std::string label = secretMenuItemName(entry);
    if (tvBGone && entry == SecretMenuEntry::TvBGone && tvBGone->isActive()) {
        label = "*" + label + "*";
    }
    return label;
}

// Gesture sequence: up, down, left, right, right, left, down, up.
constexpr uint8_t secretGestureSequence[] = {
    static_cast<uint8_t>(INPUT_BROKER_UP),
    static_cast<uint8_t>(INPUT_BROKER_DOWN),
    static_cast<uint8_t>(INPUT_BROKER_LEFT),
    static_cast<uint8_t>(INPUT_BROKER_RIGHT),
    static_cast<uint8_t>(INPUT_BROKER_RIGHT),
    static_cast<uint8_t>(INPUT_BROKER_LEFT),
    static_cast<uint8_t>(INPUT_BROKER_DOWN),
    static_cast<uint8_t>(INPUT_BROKER_UP)};

constexpr size_t secretGestureLength = sizeof(secretGestureSequence) / sizeof(secretGestureSequence[0]);

#if HAS_TFT
constexpr uint16_t secretMenuTextColor = COLOR565(0x3B, 0x82, 0xF6);   // blue
constexpr uint16_t secretMenuAccentColor = COLOR565(0xFF, 0xEB, 0x3B); // yellow
constexpr uint16_t secretMenuHeaderColor = COLOR565(0xF4, 0x43, 0x36); // red
#endif

struct SecretWifiScanResult
{
    std::string ssid;
    int32_t rssi = 0;
    uint8_t channel = 0;
#if HAS_WIFI && defined(ARCH_ESP32)
    wifi_auth_mode_t security = WIFI_AUTH_OPEN;
#else
    uint8_t security = 0;
#endif
    std::array<uint8_t, 6> bssid{};
};

static std::vector<SecretWifiScanResult> wifiScanResults;
static size_t wifiScanSelection = 0;
static bool wifiScanInProgress = false;
static std::string wifiScanStatus = "Tap to scan";
static bool wifiPreferredValid = false;
static std::array<uint8_t, 6> wifiPreferredBssid{};
static std::string wifiPreferredSsid;
static size_t stationApSelection = 0;
static size_t stationStaSelection = 0;

#if HAS_WIFI && defined(ARCH_ESP32)
static const char *wifiAuthLabel(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/3";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI";
    default:
        return "OTHER";
    }
}
#else
static const char *wifiAuthLabel(uint8_t) { return "N/A"; }
#endif

static bool wifiEntryIsPreferred(const SecretWifiScanResult &entry)
{
    return wifiPreferredValid && entry.bssid == wifiPreferredBssid;
}

static std::string macToString(const std::array<uint8_t, 6> &mac)
{
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

#if defined(T_WATCH_S3)
constexpr int dbMicClkPin = 44;
constexpr int dbMicDataPin = 47;
#else
constexpr int dbMicClkPin = -1;
constexpr int dbMicDataPin = -1;
#endif
constexpr uint32_t pactCountdownMs = 3000;
constexpr uint32_t pactBeepDurationMs = 180;
constexpr uint32_t pactShotMinGapMs = 85;
constexpr float pactShotThresholdDbfs = -18.0f;
constexpr size_t pactSplitsPerPage = 5;
} // namespace

static std::string secretMenuTargetString()
{
#if HAS_WIFI && defined(ARCH_ESP32)
    if (!wifiPreferredValid)
        return "*Target none*";

    std::string label;
    auto &tracker = marauder::StationTracker::instance();
    if (tracker.hasSelection()) {
        if (const auto *sta = tracker.getSelectedStation())
            label = macToString(sta->mac);
    }
    if (label.empty()) {
        if (!wifiPreferredSsid.empty())
            label = wifiPreferredSsid;
        else
            label = macToString(wifiPreferredBssid);
        if (label.empty())
            label = "<hidden>";
    }
    return "*Target " + label + "*";
#else
    return "*Target none*";
#endif
}

#if HAS_GPS
// GeoCoord object for the screen
GeoCoord geoCoord;
#endif

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

// Usage: int stringWidth = formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display);
// End Functions to write date/time to the screen

extern bool hasUnreadMessage;

// ==============================
// Overlay Alert Banner Renderer
// ==============================
// Displays a temporary centered banner message (e.g., warning, status, etc.)
// The banner appears in the center of the screen and disappears after the specified duration

void Screen::showSimpleBanner(const char *message, uint32_t durationMs)
{
    BannerOverlayOptions options;
    options.message = message;
    options.durationMs = durationMs;
    options.notificationType = notificationTypeEnum::text_banner;
    showOverlayBanner(options);
}

// Called to trigger a banner with custom message and duration
void Screen::showOverlayBanner(BannerOverlayOptions banner_overlay_options)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, banner_overlay_options.message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil =
        (banner_overlay_options.durationMs == 0) ? 0 : millis() + banner_overlay_options.durationMs;
    NotificationRenderer::optionsArrayPtr = banner_overlay_options.optionsArrayPtr;
    NotificationRenderer::optionsEnumPtr = banner_overlay_options.optionsEnumPtr;
    NotificationRenderer::alertBannerOptions = banner_overlay_options.optionsCount;
    NotificationRenderer::alertBannerCallback = banner_overlay_options.bannerCallback;
    NotificationRenderer::curSelected = banner_overlay_options.InitialSelected;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::current_notification_type = notificationTypeEnum::selection_picker;
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

// Called to trigger a banner with custom message and duration
void Screen::showNodePicker(const char *message, uint32_t durationMs, std::function<void(uint32_t)> bannerCallback)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    nodeDB->pause_sort(true);
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::alertBannerCallback = bannerCallback;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::curSelected = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::node_picker;

    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

// Called to trigger a banner with custom message and duration
void Screen::showNumberPicker(const char *message, uint32_t durationMs, uint8_t digits,
                              std::function<void(uint32_t)> bannerCallback)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::alertBannerCallback = bannerCallback;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::curSelected = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::number_picker;
    NotificationRenderer::numDigits = digits;
    NotificationRenderer::currentNumber = 0;

    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

void Screen::showTextInput(const char *header, const char *initialText, uint32_t durationMs,
                           std::function<void(const std::string &)> textCallback)
{
    LOG_INFO("showTextInput called with header='%s', durationMs=%d", header ? header : "NULL", durationMs);

    // Start OnScreenKeyboardModule session (non-touch variant)
    OnScreenKeyboardModule::instance().start(header, initialText, durationMs, textCallback);
    NotificationRenderer::textInputCallback = textCallback;

    // Store the message and set the expiration timestamp (use same pattern as other notifications)
    strncpy(NotificationRenderer::alertBannerMessage, header ? header : "Text Input", 255);
    NotificationRenderer::alertBannerMessage[255] = '\0';
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::current_notification_type = notificationTypeEnum::text_input;

    // Set the overlay using the same pattern as other notification types
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

void Screen::showSecretToolsMenu()
{
    enum optionsNumbers {
        Back,
        WifiAttacks,
        WifiScanner,
        StationBrowser,
        BatteryMeter,
        ToneGenerator,
        DbMeter,
        PactTimer,
        Detonate,
        MsdCalculator,
        TvBGone,
        enumEnd
    };

    static const char *optionsArray[enumEnd] = {"Back"};
    static int optionsEnumArray[enumEnd] = {Back};
    int options = 1;

#if HAS_WIFI && defined(ARCH_ESP32)
    optionsArray[options] = "WiFi Attacks";
    optionsEnumArray[options++] = WifiAttacks;
#endif
#if HAS_WIFI
    optionsArray[options] = "WiFi Scanner";
    optionsEnumArray[options++] = WifiScanner;
#endif
#if HAS_WIFI && defined(ARCH_ESP32)
    optionsArray[options] = "Station Browser";
    optionsEnumArray[options++] = StationBrowser;
#endif

    optionsArray[options] = "Battery Meter";
    optionsEnumArray[options++] = BatteryMeter;
    optionsArray[options] = "Tone Generator";
    optionsEnumArray[options++] = ToneGenerator;
    optionsArray[options] = "dB Meter";
    optionsEnumArray[options++] = DbMeter;
    optionsArray[options] = "PACT Timer";
    optionsEnumArray[options++] = PactTimer;
    optionsArray[options] = "Detonate";
    optionsEnumArray[options++] = Detonate;
    optionsArray[options] = "MSD Calculator";
    optionsEnumArray[options++] = MsdCalculator;
#if defined(ARDUINO_ARCH_ESP32)
    optionsArray[options] = (tvBGone && tvBGone->isActive()) ? "Stop TV-B-Gone" : "TV-B-Gone";
    optionsEnumArray[options++] = TvBGone;
#endif

    BannerOverlayOptions bannerOptions;
    bannerOptions.message = "Secret Tools";
    bannerOptions.optionsArrayPtr = optionsArray;
    bannerOptions.optionsEnumPtr = optionsEnumArray;
    bannerOptions.optionsCount = options;
    bannerOptions.bannerCallback = [this](int selected) -> void {
        auto prepareSecretToolMode = [this]() {
            if (!secretMenuVisible) {
                secretMenuVisible = true;
                secretMenuSelection = 0;
                wifiAttackSelection = 0;
                wifiScanSelection = 0;
                wifiScanResults.clear();
                wifiScanStatus = "Tap to scan";
                wifiScanInProgress = false;
                stationApSelection = 0;
                stationStaSelection = 0;
            }
            secretGestureProgress = 0;
        };

        switch (selected) {
        case Back:
            menuHandler::menuQueue = menuHandler::test_menu;
            runNow();
            break;
        case WifiAttacks:
            prepareSecretToolMode();
            secretMenuMode = SecretMenuMode::WifiAttacks;
            wifiAttackSelection = 0;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case WifiScanner:
            prepareSecretToolMode();
            secretMenuMode = SecretMenuMode::WifiScanner;
            wifiScanSelection = 0;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case StationBrowser:
            prepareSecretToolMode();
            showStationBrowser();
            break;
        case BatteryMeter:
            hideSecretMenu();
            startBattMeterMode();
            break;
        case ToneGenerator:
            prepareSecretToolMode();
            startToneGeneratorMode();
            break;
        case DbMeter:
            prepareSecretToolMode();
            startDbMeterMode();
            break;
        case PactTimer:
            prepareSecretToolMode();
            startPactTimerMode();
            break;
        case Detonate:
            prepareSecretToolMode();
            enterDetonateMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case MsdCalculator:
            prepareSecretToolMode();
            startMsdCalculatorMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case TvBGone:
            if (tvBGone && tvBGone->isActive())
                stopTvBGoneTool();
            else
                startTvBGoneTool();
            break;
        default:
            break;
        }
    };

    showOverlayBanner(bannerOptions);
}

static void drawModuleFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    uint8_t module_frame;
    // there's a little but in the UI transition code
    // where it invokes the function at the correct offset
    // in the array of "drawScreen" functions; however,
    // the passed-state doesn't quite reflect the "current"
    // screen, so we have to detect it.
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == TransitionRelationship_INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        module_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the module frame that's aligned with the current frame
        module_frame = state->currentFrame;
        // LOG_DEBUG("Screen is not in transition.  Frame: %d", module_frame);
    }
    // LOG_DEBUG("Draw Module Frame %d", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

// Ignore messages originating from phone (from the current node 0x0) unless range test or store and forward module are enabled
static bool shouldDrawMessage(const meshtastic_MeshPacket *packet)
{
    return packet->from != 0 && !moduleConfig.store_forward.enabled;
}

/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 *
 * We keep a series of "after you've gone 10 meters, what is your heading since
 * the last reference point?"
 */
float Screen::estimatedHeading(double lat, double lon)
{
    static double oldLat, oldLon;
    static float b;

    if (oldLat == 0) {
        // just prepare for next time
        oldLat = lat;
        oldLon = lon;

        return b;
    }

    float d = GeoCoord::latLongToMeter(oldLat, oldLon, lat, lon);
    if (d < 10) // haven't moved enough, just keep current bearing
        return b;

    b = GeoCoord::bearing(oldLat, oldLon, lat, lon) * RAD_TO_DEG;
    oldLat = lat;
    oldLon = lon;

    return b;
}

/// We will skip one node - the one for us, so we just blindly loop over all
/// nodes
static int8_t prevFrame = -1;

// Combined dynamic node list frame cycling through LastHeard, HopSignal, and Distance modes
// Uses a single frame and changes data every few seconds (E-Ink variant is separate)

#if defined(ESP_PLATFORM) && (defined(USE_ST7789) || defined(USE_ST7796))
SPIClass SPI1(HSPI);
#endif

Screen::Screen(ScanI2C::DeviceAddress address, meshtastic_Config_DisplayConfig_OledType screenType, OLEDDISPLAY_GEOMETRY geometry)
    : concurrency::OSThread("Screen"), address_found(address), model(screenType), geometry(geometry), cmdQueue(32)
{
    graphics::normalFrames = new FrameCallback[MAX_NUM_NODES + NUM_EXTRA_FRAMES];

    LOG_INFO("Protobuf Value uiconfig.screen_rgb_color: %d", uiconfig.screen_rgb_color);
    int32_t rawRGB = uiconfig.screen_rgb_color;
    if (rawRGB > 0 && rawRGB <= 255255255) {
        uint8_t TFT_MESH_r = (rawRGB >> 16) & 0xFF;
        uint8_t TFT_MESH_g = (rawRGB >> 8) & 0xFF;
        uint8_t TFT_MESH_b = rawRGB & 0xFF;
        LOG_INFO("Values of r,g,b: %d, %d, %d", TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);

        if (TFT_MESH_r <= 255 && TFT_MESH_g <= 255 && TFT_MESH_b <= 255) {
            TFT_MESH = COLOR565(TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);
        }
    }

#if defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SH1107_128_64)
    dispdev = new SH1106Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7789)
#ifdef ESP_PLATFORM
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT, ST7789_SDA,
                            ST7789_MISO, ST7789_SCK);
#else
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
#elif defined(USE_ST7796)
#ifdef ESP_PLATFORM
    dispdev = new ST7796Spi(&SPI1, ST7796_RESET, ST7796_RS, ST7796_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT, ST7796_SDA,
                            ST7796_MISO, ST7796_SCK, TFT_SPI_FREQUENCY);
#else
    dispdev = new ST7796Spi(&SPI1, ST7796_RESET, ST7796_RS, ST7796_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
#elif defined(USE_SSD1306)
    dispdev = new SSD1306Wire(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_SPISSD1306)
    dispdev = new SSD1306Spi(SSD1306_RESET, SSD1306_RS, SSD1306_NSS, GEOMETRY_64_48);
    if (!dispdev->init()) {
        LOG_DEBUG("Error: SSD1306 not detected!");
    } else {
        static_cast<SSD1306Spi *>(dispdev)->setHorizontalOffset(32);
        LOG_INFO("SSD1306 init success");
    }
#elif defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7789_CS) ||    \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS) || defined(ST7796_CS) || defined(HACKADAY_COMMUNICATOR)
    dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && !defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDisplay(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDynamicDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7567)
    dispdev = new ST7567Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        if (portduino_config.displayPanel != no_screen) {
            LOG_DEBUG("Make TFTDisplay!");
            dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
        } else {
            dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                                       (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
            isAUTOOled = true;
        }
    }
#else
    dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                               (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
    isAUTOOled = true;
#endif

#if defined(USE_ST7789)
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#elif defined(USE_ST7796)
    static_cast<ST7796Spi *>(dispdev)->setRGB(TFT_MESH);
#endif

    ui = new OLEDDisplayUi(dispdev);
    cmdQueue.setReader(this);
}

Screen::~Screen()
{
    delete[] graphics::normalFrames;
}

/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
#ifdef USE_EINK
    setOn(false, graphics::UIRenderer::drawDeepSleepFrame);
#else
    // Without E-Ink display:
    setOn(false);
#endif
}

void Screen::handleSetOn(bool on, FrameCallback einkScreensaver)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            LOG_INFO("Turn on screen");
            powerMon->setState(meshtastic_PowerMon_State_Screen_On);
#ifdef T_WATCH_S3
            PMU->enablePowerOutput(XPOWERS_ALDO2);
#endif

#if defined(MUZI_BASE)
            dispdev->init();
            dispdev->setBrightness(brightness);
            dispdev->flipScreenVertically();
            dispdev->resetDisplay();
            digitalWrite(SCREEN_12V_ENABLE, HIGH);
            delay(100);
#endif
#if !ARCH_PORTDUINO
            dispdev->displayOn();
#endif

#ifdef PIN_EINK_EN
            if (uiconfig.screen_brightness == 1)
                digitalWrite(PIN_EINK_EN, HIGH);
#elif defined(PCA_PIN_EINK_EN)
            if (uiconfig.screen_brightness > 0)
                io.digitalWrite(PCA_PIN_EINK_EN, HIGH);
#endif

#if defined(ST7789_CS) &&                                                                                                        \
    !defined(M5STACK) // set display brightness when turning on screens. Just moved function from TFTDisplay to here.
            static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

            dispdev->displayOn();
#if defined(HELTEC_TRACKER_V1_X) || defined(HELTEC_WIRELESS_TRACKER_V2)
            ui->init();
#endif
#ifdef USE_ST7789
            pinMode(VTFT_CTRL, OUTPUT);
            digitalWrite(VTFT_CTRL, LOW);
            ui->init();
#ifdef ESP_PLATFORM
            analogWrite(VTFT_LEDA, BRIGHTNESS_DEFAULT);
#else
            pinMode(VTFT_LEDA, OUTPUT);
            digitalWrite(VTFT_LEDA, TFT_BACKLIGHT_ON);
#endif
#endif
#ifdef USE_ST7796
            ui->init();
#ifdef ESP_PLATFORM
            analogWrite(VTFT_LEDA, BRIGHTNESS_DEFAULT);
#else
            pinMode(VTFT_LEDA, OUTPUT);
            digitalWrite(VTFT_LEDA, TFT_BACKLIGHT_ON);
#endif
#endif
            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
            powerMon->clearState(meshtastic_PowerMon_State_Screen_On);
#ifdef USE_EINK
            // eInkScreensaver parameter is usually NULL (default argument), default frame used instead
            setScreensaverFrames(einkScreensaver);
#endif

#ifdef PIN_EINK_EN
            digitalWrite(PIN_EINK_EN, LOW);
#elif defined(PCA_PIN_EINK_EN)
            io.digitalWrite(PCA_PIN_EINK_EN, LOW);
#endif

            dispdev->displayOff();

#ifdef SCREEN_12V_ENABLE
            digitalWrite(SCREEN_12V_ENABLE, LOW);
#endif
#ifdef USE_ST7789
            SPI1.end();
#if defined(ARCH_ESP32)
            pinMode(VTFT_LEDA, ANALOG);
            pinMode(VTFT_CTRL, ANALOG);
            pinMode(ST7789_RESET, ANALOG);
            pinMode(ST7789_RS, ANALOG);
            pinMode(ST7789_NSS, ANALOG);
#else
            nrf_gpio_cfg_default(VTFT_LEDA);
            nrf_gpio_cfg_default(VTFT_CTRL);
            nrf_gpio_cfg_default(ST7789_RESET);
            nrf_gpio_cfg_default(ST7789_RS);
            nrf_gpio_cfg_default(ST7789_NSS);
#endif
#endif
#ifdef USE_ST7796
            SPI1.end();
#if defined(ARCH_ESP32)
            pinMode(VTFT_LEDA, OUTPUT);
            digitalWrite(VTFT_LEDA, LOW);
            pinMode(ST7796_RESET, ANALOG);
            pinMode(ST7796_RS, ANALOG);
            pinMode(ST7796_NSS, ANALOG);
#else
            nrf_gpio_cfg_default(VTFT_LEDA);
            nrf_gpio_cfg_default(ST7796_RESET);
            nrf_gpio_cfg_default(ST7796_RS);
            nrf_gpio_cfg_default(ST7796_NSS);
#endif
#endif

#ifdef T_WATCH_S3
            PMU->disablePowerOutput(XPOWERS_ALDO2);
#endif
            enabled = false;
        }
        screenOn = on;
    }
}

void Screen::setup()
{

    // === Enable display rendering ===
    useDisplay = true;

    // === Load saved brightness from UI config ===
    // For OLED displays (SSD1306), default brightness is 255 if not set
    if (uiconfig.screen_brightness == 0) {
#if defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
        brightness = 255; // Default for OLED
#else
        brightness = BRIGHTNESS_DEFAULT;
#endif
    } else {
        brightness = uiconfig.screen_brightness;
    }

    // === Detect OLED subtype (if supported by board variant) ===
#ifdef AutoOLEDWire_h
    if (isAUTOOled)
        static_cast<AutoOLEDWire *>(dispdev)->setDetected(model);
#endif

#if defined(USE_SH1107_128_64) || defined(USE_SH1107)
    static_cast<SH1106Wire *>(dispdev)->setSubtype(7);
#endif

#if defined(USE_ST7789) && defined(TFT_MESH)
    // Apply custom RGB color (e.g. Heltec T114/T190)
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#endif
#if defined(MUZI_BASE)
    dispdev->delayPoweron = true;
#endif
#if defined(USE_ST7796) && defined(TFT_MESH)
    // Custom text color, if defined in variant.h
    static_cast<ST7796Spi *>(dispdev)->setRGB(TFT_MESH);
#endif

    // === Initialize display and UI system ===
    ui->init();
    displayWidth = dispdev->width();
    displayHeight = dispdev->height();

    ui->setTimePerTransition(0);           // Disable animation delays
    ui->setIndicatorPosition(BOTTOM);      // Not used (indicators disabled below)
    ui->setIndicatorDirection(LEFT_RIGHT); // Not used (indicators disabled below)
    ui->setFrameAnimation(SLIDE_LEFT);     // Used only when indicators are active
    ui->disableAllIndicators();            // Disable page indicator dots
    ui->getUiState()->userData = this;     // Allow static callbacks to access Screen instance

    // === Apply loaded brightness ===
#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#elif defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SPISSD1306)
    dispdev->setBrightness(brightness);
#endif
    LOG_INFO("Applied screen brightness: %d", brightness);

    // === Set custom overlay callbacks ===
    static OverlayCallback overlays[] = {
        graphics::UIRenderer::drawNavigationBar // Custom indicator icons for each frame
    };
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    // === Enable UTF-8 to display mapping ===
    dispdev->setFontTableLookupFunction(customFontTableLookup);

#ifdef USERPREFS_OEM_TEXT
    logo_timeout *= 2; // Give more time for branded boot logos
#endif

    // === Configure alert frames (e.g., "Resuming..." or region name) ===
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip slow refresh
    alertFrames[0] = [this](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
#ifdef ARCH_ESP32
        if (wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_EXT1)
            graphics::UIRenderer::drawFrameText(display, state, x, y, "Resuming...");
        else
#endif
        {
            const char *region = myRegion ? myRegion->name : nullptr;
            graphics::UIRenderer::drawIconScreen(region, display, state, x, y);
        }
    };
    ui->setFrames(alertFrames, 1);
    ui->disableAutoTransition(); // Require manual navigation between frames

    // === Log buffer for on-screen logs (3 lines max) ===
    dispdev->setLogBuffer(3, 32);

    // === Optional screen mirroring or flipping (e.g. for T-Beam orientation) ===
#ifdef SCREEN_MIRROR
    dispdev->mirrorScreen();
#else
    if (!config.display.flip_screen) {
#if defined(ST7701_CS) || defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7789_CS) ||      \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS) || defined(ST7796_CS) || defined(HACKADAY_COMMUNICATOR)
        static_cast<TFTDisplay *>(dispdev)->flipScreenVertically();
#elif defined(USE_ST7789)
        static_cast<ST7789Spi *>(dispdev)->flipScreenVertically();
#elif defined(USE_ST7796)
        static_cast<ST7796Spi *>(dispdev)->mirrorScreen();
#elif !defined(M5STACK_UNITC6L)
        dispdev->flipScreenVertically();
#endif
    }
#endif

    // === Generate device ID from MAC address ===
    uint8_t dmac[6];
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);

#if ARCH_PORTDUINO
    handleSetOn(false); // Ensure proper init for Arduino targets
#endif

    // === Turn on display and trigger first draw ===
    handleSetOn(true);
    determineResolution(dispdev->height(), dispdev->width());
    ui->update();
#ifndef USE_EINK
    ui->update(); // Some SSD1306 clones drop the first draw, so run twice
#endif
    serialSinceMsec = millis();

#if ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        if (portduino_config.touchscreenModule) {
            touchScreenImpl1 =
                new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
            touchScreenImpl1->init();
        }
    }
#elif HAS_TOUCHSCREEN && !defined(USE_EINK) && !HAS_CST226SE
    touchScreenImpl1 =
        new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
    touchScreenImpl1->init();
#endif

    // === Subscribe to device status updates ===
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);

#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe(adminModule);
#endif
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);
    if (inputBroker)
        inputObserver.observe(inputBroker);

    // === Notify modules that support UI events ===
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::setOn(bool on, FrameCallback einkScreensaver)
{
#if defined(T_LORA_PAGER)
    if (cardKbI2cImpl)
        cardKbI2cImpl->toggleBacklight(on);
#endif
    if (!on)
        // We handle off commands immediately, because they might be called because the CPU is shutting down
        handleSetOn(false, einkScreensaver);
    else
        enqueueCmd(ScreenCmd{.cmd = Cmd::SET_ON});
}

void Screen::forceDisplay(bool forceUiUpdate)
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef USE_EINK
    // If requested, make sure queued commands are run, and UI has rendered a new frame
    if (forceUiUpdate) {
        // Force a display refresh, in addition to the UI update
        // Changing the GPS status bar icon apparently doesn't register as a change in image
        // (False negative of the image hashing algorithm used to skip identical frames)
        EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST);

        // No delay between UI frame rendering
        setFastFramerate();

        // Make sure all CMDs have run first
        while (!cmdQueue.isEmpty())
            runOnce();

        // Ensure at least one frame has drawn
        uint64_t startUpdate;
        do {
            startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
            delay(10);
            ui->update();
        } while (ui->getUiState()->lastUpdate < startUpdate);

        // Return to normal frame rate
        targetFramerate = IDLE_FRAMERATE;
        ui->setTargetFPS(targetFramerate);
    }

    // Tell EInk class to update the display
    static_cast<EInkDisplay *>(dispdev)->forceDisplay();
#else
    // No delay between UI frame rendering
    if (forceUiUpdate) {
        setFastFramerate();
    }
#endif
}

static uint32_t lastScreenTransition;

int32_t Screen::runOnce()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        enabled = false;
        return RUN_SAME;
    }

    if (displayHeight == 0) {
        displayHeight = dispdev->getHeight();
    }
    menuHandler::handleMenuSwitch(dispdev);

    // Show boot screen for first logo_timeout seconds, then switch to normal operation.
    // serialSinceMsec adjusts for additional serial wait time during nRF52 bootup
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > (logo_timeout + serialSinceMsec))) {
        LOG_INFO("Done with boot screen");
        stopBootScreen();
        showingBootScreen = false;
    }

#ifdef USERPREFS_OEM_TEXT
    static bool showingOEMBootScreen = true;
    if (showingOEMBootScreen && (millis() > ((logo_timeout / 2) + serialSinceMsec))) {
        LOG_INFO("Switch to OEM screen...");
        // Change frames.
        static FrameCallback bootOEMFrames[] = {graphics::UIRenderer::drawOEMBootScreen};
        static const int bootOEMFrameCount = sizeof(bootOEMFrames) / sizeof(bootOEMFrames[0]);
        ui->setFrames(bootOEMFrames, bootOEMFrameCount);
        ui->update();
#ifndef USE_EINK
        ui->update();
#endif
        showingOEMBootScreen = false;
    }
#endif

#ifndef DISABLE_WELCOME_UNSET
    if (!NotificationRenderer::isOverlayBannerShowing() && config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
#if defined(M5STACK_UNITC6L)
        menuHandler::LoraRegionPicker();
#else
        menuHandler::OnboardMessage();
#endif
    }
#endif
    if (!NotificationRenderer::isOverlayBannerShowing() && rebootAtMsec != 0) {
        showSimpleBanner("Rebooting...", 0);
    }

    // Process incoming commands.
    for (;;) {
        ScreenCmd cmd;
        if (!cmdQueue.dequeue(&cmd, 0)) {
            break;
        }
        switch (cmd.cmd) {
        case Cmd::SET_ON:
            handleSetOn(true);
            break;
        case Cmd::SET_OFF:
            handleSetOn(false);
            break;
        case Cmd::ON_PRESS:
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                handleOnPress();
            }
            break;
        case Cmd::SHOW_PREV_FRAME:
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                handleShowPrevFrame();
            }
            break;
        case Cmd::SHOW_NEXT_FRAME:
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                handleShowNextFrame();
            }
            break;
        case Cmd::START_ALERT_FRAME: {
            showingBootScreen = false; // this should avoid the edge case where an alert triggers before the boot screen goes away
            showingNormalScreen = false;
            NotificationRenderer::pauseBanner = true;
            alertFrames[0] = alertFrame;
#ifdef USE_EINK
            EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
            EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
            handleSetOn(true); // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
#endif
            setFrameImmediateDraw(alertFrames);
            break;
        }
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_ALERT_FRAME:
            NotificationRenderer::pauseBanner = false;
        case Cmd::STOP_BOOT_SCREEN:
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // E-Ink: Explicitly use full-refresh for next frame
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                setFrames();
            }
            break;
        case Cmd::NOOP:
            break;
        default:
            LOG_ERROR("Invalid screen cmd");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        enabled = false;
        return 0;
    }

    // this must be before the frameState == FIXED check, because we always
    // want to draw at least one FIXED frame before doing forceDisplay
    ui->update();

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because
    // otherwise that breaks animations.

    if (targetFramerate != IDLE_FRAMERATE && ui->getUiState()->frameState == FIXED) {
        // oldFrameState = ui->getUiState()->frameState;
        targetFramerate = IDLE_FRAMERATE;

        ui->setTargetFPS(targetFramerate);
        forceDisplay();
    }

    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
        if (config.display.auto_screen_carousel_secs > 0 &&
            NotificationRenderer::current_notification_type != notificationTypeEnum::text_input &&
            !Throttle::isWithinTimespanMs(lastScreenTransition, config.display.auto_screen_carousel_secs * 1000)) {

            // If an E-Ink display struggles with fast refresh, force carousel to use full refresh instead
            // Carousel is potentially a major source of E-Ink display wear
#if !defined(EINK_BACKGROUND_USES_FAST)
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC);
#endif

            LOG_DEBUG("LastScreenTransition exceeded %ums transition to next frame", (millis() - lastScreenTransition));
            handleOnPress();
        }
    }

    // LOG_DEBUG("want fps %d, fixed=%d", targetFramerate,
    // ui->getUiState()->frameState); If we are scrolling we need to be called
    // soon, otherwise just 1 fps (to save CPU) We also ask to be called twice
    // as fast as we really need so that any rounding errors still result with
    // the correct framerate
#if HAS_WIFI
    msdCalculatorServer.loop();
#endif
    updatePactTimer();
    return (1000 / targetFramerate);
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setSSLFrames()
{
    if (address_found.address) {
        // LOG_DEBUG("Show SSL frames");
        static FrameCallback sslFrames[] = {NotificationRenderer::drawSSLScreen};
        ui->setFrames(sslFrames, 1);
        ui->update();
    }
}

#ifdef USE_EINK
/// Determine which screensaver frame to use, then set the FrameCallback
void Screen::setScreensaverFrames(FrameCallback einkScreensaver)
{
    // Retain specified frame / overlay callback beyond scope of this method
    static FrameCallback screensaverFrame;
    static OverlayCallback screensaverOverlay;

#if defined(HAS_EINK_ASYNCFULL) && defined(USE_EINK_DYNAMICDISPLAY)
    // Join (await) a currently running async refresh, then run the post-update code.
    // Avoid skipping of screensaver frame. Would otherwise be handled by NotifiedWorkerThread.
    EINK_JOIN_ASYNCREFRESH(dispdev);
#endif

    // If: one-off screensaver frame passed as argument. Handles doDeepSleep()
    if (einkScreensaver != NULL) {
        screensaverFrame = einkScreensaver;
        ui->setFrames(&screensaverFrame, 1);
    }

    // Else, display the usual "overlay" screensaver
    else {
        screensaverOverlay = graphics::UIRenderer::drawScreensaverOverlay;
        ui->setOverlays(&screensaverOverlay, 1);
    }

    // Request new frame, ASAP
    setFastFramerate();
    uint64_t startUpdate;
    do {
        startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
        delay(1);
        ui->update();
    } while (ui->getUiState()->lastUpdate < startUpdate);

    // Old EInkDisplay class
#if !defined(USE_EINK_DYNAMICDISPLAY)
    static_cast<EInkDisplay *>(dispdev)->forceDisplay(0); // Screen::forceDisplay(), but override rate-limit
#endif

    // Prepare now for next frame, shown when display wakes
    ui->setOverlays(NULL, 0);  // Clear overlay
    setFrames(FOCUS_PRESERVE); // Return to normal display updates, showing same frame as before screensaver, ideally

    // Pick a refresh method, for when display wakes
#ifdef EINK_HASQUIRK_GHOSTING
    EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // Really ugly to see ghosting from "screen paused"
#else
    EINK_ADD_FRAMEFLAG(dispdev, RESPONSIVE); // Really nice to wake screen with a fast-refresh
#endif
}
#endif

// Regenerate the normal set of frames, focusing a specific frame if requested
// Called when a frame should be added / removed, or custom frames should be cleared
void Screen::setFrames(FrameFocus focus)
{
    // Block setFrames calls when virtual keyboard is active to prevent overlay interference
    if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
        return;
    }

    uint8_t originalPosition = ui->getUiState()->currentFrame;
    uint8_t previousFrameCount = framesetInfo.frameCount;
    FramesetInfo fsi; // Location of specific frames, for applying focus parameter

    graphics::UIRenderer::rebuildFavoritedNodes();

    LOG_DEBUG("Show standard frames");
    showingNormalScreen = true;

    indicatorIcons.clear();

    size_t numframes = 0;

    // If we have a critical fault, show it first
    fsi.positions.fault = numframes;
    if (error_code) {
        normalFrames[numframes++] = NotificationRenderer::drawCriticalFaultFrame;
        indicatorIcons.push_back(icon_error);
        focus = FOCUS_FAULT; // Change our "focus" parameter, to ensure we show the fault frame
    }

#if defined(DISPLAY_CLOCK_FRAME)
    if (!hiddenFrames.clock) {
        fsi.positions.clock = numframes;
#if defined(M5STACK_UNITC6L)
        normalFrames[numframes++] = graphics::ClockRenderer::drawAnalogClockFrame;
#else
        normalFrames[numframes++] = uiconfig.is_clockface_analog ? graphics::ClockRenderer::drawAnalogClockFrame
                                                                 : graphics::ClockRenderer::drawDigitalClockFrame;
#endif
        indicatorIcons.push_back(digital_icon_clock);
    }
#endif

    // Declare this early so it’s available in FOCUS_PRESERVE block
    bool willInsertTextMessage = shouldDrawMessage(&devicestate.rx_text_message);

    if (!hiddenFrames.home) {
        fsi.positions.home = numframes;
        normalFrames[numframes++] = graphics::UIRenderer::drawDeviceFocused;
        indicatorIcons.push_back(icon_home);
    }

    fsi.positions.textMessage = numframes;
    normalFrames[numframes++] = graphics::MessageRenderer::drawTextMessageFrame;
    indicatorIcons.push_back(icon_mail);

#ifndef USE_EINK
    if (!hiddenFrames.nodelist) {
        fsi.positions.nodelist = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawDynamicNodeListScreen;
        indicatorIcons.push_back(icon_nodes);
    }
#endif

// Show detailed node views only on E-Ink builds
#ifdef USE_EINK
    if (!hiddenFrames.nodelist_lastheard) {
        fsi.positions.nodelist_lastheard = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawLastHeardScreen;
        indicatorIcons.push_back(icon_nodes);
    }
    if (!hiddenFrames.nodelist_hopsignal) {
        fsi.positions.nodelist_hopsignal = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawHopSignalScreen;
        indicatorIcons.push_back(icon_signal);
    }
    if (!hiddenFrames.nodelist_distance) {
        fsi.positions.nodelist_distance = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawDistanceScreen;
        indicatorIcons.push_back(icon_distance);
    }
#endif
#if HAS_GPS
    if (!hiddenFrames.nodelist_bearings) {
        fsi.positions.nodelist_bearings = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawNodeListWithCompasses;
        indicatorIcons.push_back(icon_list);
    }
    if (!hiddenFrames.gps) {
        fsi.positions.gps = numframes;
        normalFrames[numframes++] = graphics::UIRenderer::drawCompassAndLocationScreen;
        indicatorIcons.push_back(icon_compass);
    }
#endif
    if (RadioLibInterface::instance && !hiddenFrames.lora) {
        fsi.positions.lora = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawLoRaFocused;
        indicatorIcons.push_back(icon_radio);
    }
    if (!hiddenFrames.system) {
        fsi.positions.system = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawSystemScreen;
        indicatorIcons.push_back(icon_system);
    }
#if !defined(DISPLAY_CLOCK_FRAME)
    if (!hiddenFrames.clock) {
        fsi.positions.clock = numframes;
        normalFrames[numframes++] = uiconfig.is_clockface_analog ? graphics::ClockRenderer::drawAnalogClockFrame
                                                                 : graphics::ClockRenderer::drawDigitalClockFrame;
        indicatorIcons.push_back(digital_icon_clock);
    }
#endif
    if (!hiddenFrames.chirpy) {
        fsi.positions.chirpy = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawChirpy;
        indicatorIcons.push_back(chirpy_small);
    }

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    if (!hiddenFrames.wifi && isWifiAvailable()) {
        fsi.positions.wifi = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawDebugInfoWiFiTrampoline;
        indicatorIcons.push_back(icon_wifi);
    }
#endif

    if (msdSummaryActive) {
        fsi.positions.msdSummary = numframes;
        normalFrames[numframes++] = &Screen::drawMsdStatusFrame;
        indicatorIcons.push_back(icon_module);
    }
    if (secretMenuVisible) {
        fsi.positions.secretMenu = numframes;
        normalFrames[numframes++] = &Screen::drawSecretMenuFrame;
        indicatorIcons.push_back(icon_module);
    }
    if (battMeterActive) {
        fsi.positions.battMeter = numframes;
        normalFrames[numframes++] = &Screen::drawBattMeterFrame;
        indicatorIcons.push_back(icon_module);
    }

    // Beware of what changes you make in this code!
    // We pass numframes into GetMeshModulesWithUIFrames() which is highly important!
    // Inside of that callback, goes over to MeshModule.cpp and we run
    // modulesWithUIFrames.resize(startIndex, nullptr), to insert nullptr
    // entries until we're ready to start building the matching entries.
    // We are doing our best to keep the normalFrames vector
    // and the moduleFrames vector in lock step.
    moduleFrames = MeshModule::GetMeshModulesWithUIFrames(numframes);
    LOG_DEBUG("Show %d module frames", moduleFrames.size());

    for (auto i = moduleFrames.begin(); i != moduleFrames.end(); ++i) {
        // Draw the module frame, using the hack described above
        if (*i != nullptr) {
            normalFrames[numframes] = drawModuleFrame;

            // Check if the module being drawn has requested focus
            // We will honor this request later, if setFrames was triggered by a UIFrameEvent
            MeshModule *m = *i;
            if (m && m->isRequestingFocus())
                fsi.positions.focusedModule = numframes;
            if (m && m == waypointModule)
                fsi.positions.waypoint = numframes;

            indicatorIcons.push_back(icon_module);
            numframes++;
        }
    }

    LOG_DEBUG("Added modules.  numframes: %d", numframes);

    // We don't show the node info of our node (if we have it yet - we should)
    size_t numMeshNodes = nodeDB->getNumMeshNodes();
    if (numMeshNodes > 0)
        numMeshNodes--;

    if (!hiddenFrames.show_favorites) {
        // Temporary array to hold favorite node frames
        std::vector<FrameCallback> favoriteFrames;

        for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
            const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            if (n && n->num != nodeDB->getNodeNum() && n->is_favorite) {
                favoriteFrames.push_back(graphics::UIRenderer::drawNodeInfo);
            }
        }

        // Insert favorite frames *after* collecting them all
        if (!favoriteFrames.empty()) {
            fsi.positions.firstFavorite = numframes;
            for (const auto &f : favoriteFrames) {
                normalFrames[numframes++] = f;
                indicatorIcons.push_back(icon_node);
            }
            fsi.positions.lastFavorite = numframes - 1;
        } else {
            fsi.positions.firstFavorite = 255;
            fsi.positions.lastFavorite = 255;
        }
    }

    fsi.frameCount = numframes;   // Total framecount is used to apply FOCUS_PRESERVE
    this->frameCount = numframes; // ✅ Save frame count for use in custom overlay
    LOG_DEBUG("Finished build frames. numframes: %d", numframes);

    ui->setFrames(normalFrames, numframes);
    ui->disableAllIndicators();

    // Add overlays: frame icons and alert banner)
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list just changed)

    // Focus on a specific frame, in the frame set we just created
    switch (focus) {
    case FOCUS_DEFAULT:
        ui->switchToFrame(fsi.positions.deviceFocused);
        break;
    case FOCUS_FAULT:
        ui->switchToFrame(fsi.positions.fault);
        break;
    case FOCUS_TEXTMESSAGE:
        hasUnreadMessage = false; // ✅ Clear when message is *viewed*
        ui->switchToFrame(fsi.positions.textMessage);
        break;
    case FOCUS_MODULE:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.focusedModule);
        break;
    case FOCUS_CLOCK:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.clock);
        break;
    case FOCUS_SYSTEM:
        ui->switchToFrame(fsi.positions.system);
        break;
    case FOCUS_SECRET:
        ui->switchToFrame(fsi.positions.secretMenu);
        break;
    case FOCUS_BATTMETER:
        ui->switchToFrame(fsi.positions.battMeter);
        break;

    case FOCUS_PRESERVE:
        //  No more adjustment — force stay on same index
        if (previousFrameCount > fsi.frameCount) {
            ui->switchToFrame(originalPosition - 1);
        } else if (previousFrameCount < fsi.frameCount) {
            ui->switchToFrame(originalPosition + 1);
        } else {
            ui->switchToFrame(originalPosition);
        }
        break;
    }

    // Store the info about this frameset, for future setFrames calls
    this->framesetInfo = fsi;

    setFastFramerate(); // Draw ASAP
}

void Screen::setFrameImmediateDraw(FrameCallback *drawFrames)
{
    ui->disableAllIndicators();
    ui->setFrames(drawFrames, 1);
    setFastFramerate();
}

void Screen::toggleFrameVisibility(const std::string &frameName)
{
#ifndef USE_EINK
    if (frameName == "nodelist") {
        hiddenFrames.nodelist = !hiddenFrames.nodelist;
    }
#endif
#ifdef USE_EINK
    if (frameName == "nodelist_lastheard") {
        hiddenFrames.nodelist_lastheard = !hiddenFrames.nodelist_lastheard;
    }
    if (frameName == "nodelist_hopsignal") {
        hiddenFrames.nodelist_hopsignal = !hiddenFrames.nodelist_hopsignal;
    }
    if (frameName == "nodelist_distance") {
        hiddenFrames.nodelist_distance = !hiddenFrames.nodelist_distance;
    }
#endif
#if HAS_GPS
    if (frameName == "nodelist_bearings") {
        hiddenFrames.nodelist_bearings = !hiddenFrames.nodelist_bearings;
    }
    if (frameName == "gps") {
        hiddenFrames.gps = !hiddenFrames.gps;
    }
#endif
    if (frameName == "lora") {
        hiddenFrames.lora = !hiddenFrames.lora;
    }
    if (frameName == "clock") {
        hiddenFrames.clock = !hiddenFrames.clock;
    }
    if (frameName == "show_favorites") {
        hiddenFrames.show_favorites = !hiddenFrames.show_favorites;
    }
    if (frameName == "chirpy") {
        hiddenFrames.chirpy = !hiddenFrames.chirpy;
    }
}

bool Screen::isFrameHidden(const std::string &frameName) const
{
#ifndef USE_EINK
    if (frameName == "nodelist")
        return hiddenFrames.nodelist;
#endif
#ifdef USE_EINK
    if (frameName == "nodelist_lastheard")
        return hiddenFrames.nodelist_lastheard;
    if (frameName == "nodelist_hopsignal")
        return hiddenFrames.nodelist_hopsignal;
    if (frameName == "nodelist_distance")
        return hiddenFrames.nodelist_distance;
#endif
#if HAS_GPS
    if (frameName == "nodelist_bearings")
        return hiddenFrames.nodelist_bearings;
    if (frameName == "gps")
        return hiddenFrames.gps;
#endif
    if (frameName == "lora")
        return hiddenFrames.lora;
    if (frameName == "clock")
        return hiddenFrames.clock;
    if (frameName == "show_favorites")
        return hiddenFrames.show_favorites;
    if (frameName == "chirpy")
        return hiddenFrames.chirpy;

    return false;
}

// Dismisses the currently displayed screen frame, if possible
// Relevant for text message, waypoint, others in future?
// Triggered with a CardKB keycombo
void Screen::hideCurrentFrame()
{
    uint8_t currentFrame = ui->getUiState()->currentFrame;
    bool dismissed = false;
    if (currentFrame == framesetInfo.positions.textMessage && devicestate.has_rx_text_message) {
        LOG_INFO("Hide Text Message");
        devicestate.has_rx_text_message = false;
        memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
    } else if (currentFrame == framesetInfo.positions.waypoint && devicestate.has_rx_waypoint) {
        LOG_DEBUG("Hide Waypoint");
        devicestate.has_rx_waypoint = false;
        hiddenFrames.waypoint = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.wifi) {
        LOG_DEBUG("Hide WiFi Screen");
        hiddenFrames.wifi = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.lora) {
        LOG_INFO("Hide LoRa");
        hiddenFrames.lora = true;
        dismissed = true;
    }

    if (dismissed) {
        setFrames(FOCUS_DEFAULT); // You could also use FOCUS_PRESERVE
    }
}

void Screen::handleStartFirmwareUpdateScreen()
{
    LOG_DEBUG("Show firmware screen");
    showingNormalScreen = false;
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // E-Ink: Explicitly use fast-refresh for next frame

    static FrameCallback frames[] = {graphics::NotificationRenderer::drawFrameFirmware};
    setFrameImmediateDraw(frames);
}

void Screen::blink()
{
    setFastFramerate();
    uint8_t count = 10;
    dispdev->setBrightness(254);
    while (count > 0) {
        dispdev->fillRect(0, 0, dispdev->getWidth(), dispdev->getHeight());
        dispdev->display();
        delay(50);
        dispdev->clear();
        dispdev->display();
        delay(50);
        count = count - 1;
    }
    // The dispdev->setBrightness does not work for t-deck display, it seems to run the setBrightness function in
    // OLEDDisplay.
    dispdev->setBrightness(brightness);
}

void Screen::increaseBrightness()
{
    brightness = ((brightness + 62) > 254) ? brightness : (brightness + 62);

#if defined(ST7789_CS)
    // run the setDisplayBrightness function. This works on t-decks
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::decreaseBrightness()
{
    brightness = (brightness < 70) ? brightness : (brightness - 62);

#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::setFunctionSymbol(std::string sym)
{
    if (std::find(functionSymbol.begin(), functionSymbol.end(), sym) == functionSymbol.end()) {
        functionSymbol.push_back(sym);
        functionSymbolString = "";
        for (auto symbol : functionSymbol) {
            functionSymbolString = symbol + " " + functionSymbolString;
        }
        setFastFramerate();
    }
}

void Screen::removeFunctionSymbol(std::string sym)
{
    functionSymbol.erase(std::remove(functionSymbol.begin(), functionSymbol.end(), sym), functionSymbol.end());
    functionSymbolString = "";
    for (auto symbol : functionSymbol) {
        functionSymbolString = symbol + " " + functionSymbolString;
    }
    setFastFramerate();
}

void Screen::handleOnPress()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowPrevFrame()
{
    // If screen was off, just wake it, otherwise go back to previous frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->previousFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowNextFrame()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

#ifndef SCREEN_TRANSITION_FRAMERATE
#define SCREEN_TRANSITION_FRAMERATE 30 // fps
#endif

void Screen::setFastFramerate()
{
#if defined(M5STACK_UNITC6L)
    dispdev->clear();
    dispdev->display();
#endif
    // We are about to start a transition so speed up fps
    targetFramerate = SCREEN_TRANSITION_FRAMERATE;

    ui->setTargetFPS(targetFramerate);
    setInterval(0); // redraw ASAP
    runASAP = true;
}

int Screen::handleStatusUpdate(const meshtastic::Status *arg)
{
    // LOG_DEBUG("Screen got status update %d", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(FOCUS_PRESERVE); // Regen the list of screen frames (returning to same frame, if possible)
        }
        nodeDB->updateGUI = false;
        break;
    case STATUS_TYPE_POWER:
        forceDisplay(true);
        break;
    }

    return 0;
}

// Handles when message is received; will jump to text message frame.
int Screen::handleTextMessage(const meshtastic_MeshPacket *packet)
{
    if (showingNormalScreen) {
        if (packet->from == 0) {
            // Outgoing message (likely sent from phone)
            devicestate.has_rx_text_message = false;
            memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
            hiddenFrames.textMessage = true;
            hasUnreadMessage = false; // Clear unread state when user replies

            setFrames(FOCUS_PRESERVE); // Stay on same frame, silently update frame list
        } else {
            // Incoming message
            devicestate.has_rx_text_message = true; // Needed to include the message frame
            hasUnreadMessage = true;                // Enables mail icon in the header
            setFrames(FOCUS_PRESERVE);              // Refresh frame list without switching view (no-op during text_input)

            // Only wake/force display if the configuration allows it
            if (shouldWakeOnReceivedMessage()) {
                setOn(true);    // Wake up the screen first
                forceDisplay(); // Forces screen redraw
            }
            // === Prepare banner/popup content ===
            const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(packet->from);
            const meshtastic_Channel channel =
                channels.getByIndex(packet->channel ? packet->channel : channels.getPrimaryIndex());
            const char *longName = (node && node->has_user) ? node->user.long_name : nullptr;

            const char *msgRaw = reinterpret_cast<const char *>(packet->decoded.payload.bytes);

            char banner[256];

            bool isAlert = false;

            if (moduleConfig.external_notification.alert_bell || moduleConfig.external_notification.alert_bell_vibra ||
                moduleConfig.external_notification.alert_bell_buzzer)
                // Check for bell character to determine if this message is an alert
                for (size_t i = 0; i < packet->decoded.payload.size && i < 100; i++) {
                    if (msgRaw[i] == ASCII_BELL) {
                        isAlert = true;
                        break;
                    }
                }

            // Unlike generic messages, alerts (when enabled via the ext notif module) ignore any
            // 'mute' preferences set to any specific node or channel.
            // If on-screen keyboard is active, show a transient popup over keyboard instead of interrupting it
            if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
                // Wake and force redraw so popup is visible immediately
                if (shouldWakeOnReceivedMessage()) {
                    setOn(true);
                    forceDisplay();
                }

                // Build popup: title = message source name, content = message text (sanitized)
                // Title
                char titleBuf[64] = {0};
                if (longName && longName[0]) {
                    // Sanitize sender name
                    std::string t = sanitizeString(longName);
                    strncpy(titleBuf, t.c_str(), sizeof(titleBuf) - 1);
                } else {
                    strncpy(titleBuf, "Message", sizeof(titleBuf) - 1);
                }

                // Content: payload bytes may not be null-terminated, remove ASCII_BELL and sanitize
                char content[256] = {0};
                {
                    std::string raw;
                    raw.reserve(packet->decoded.payload.size);
                    for (size_t i = 0; i < packet->decoded.payload.size; ++i) {
                        char c = msgRaw[i];
                        if (c == ASCII_BELL)
                            continue; // strip bell
                        raw.push_back(c);
                    }
                    std::string sanitized = sanitizeString(raw);
                    strncpy(content, sanitized.c_str(), sizeof(content) - 1);
                }

                NotificationRenderer::showKeyboardMessagePopupWithTitle(titleBuf, content, 3000);

// Maintain existing buzzer behavior on M5 if applicable
#if defined(M5STACK_UNITC6L)
                if (config.device.buzzer_mode != meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY ||
                    (isAlert && moduleConfig.external_notification.alert_bell_buzzer) ||
                    (!isBroadcast(packet->to) && isToUs(packet))) {
                    playLongBeep();
                }
#endif
            } else {
                // No keyboard active: use regular banner flow, respecting mute settings
                if (isAlert) {
                    if (longName && longName[0]) {
                        snprintf(banner, sizeof(banner), "Alert Received from\n%s", longName);
                    } else {
                        strcpy(banner, "Alert Received");
                    }
                    screen->showSimpleBanner(banner, 3000);
                } else if (!channel.settings.has_module_settings || !channel.settings.module_settings.is_muted) {
                    if (longName && longName[0]) {
#if defined(M5STACK_UNITC6L)
                        strcpy(banner, "New Message");
#else
                        snprintf(banner, sizeof(banner), "New Message from\n%s", longName);
#endif
                    } else {
                        strcpy(banner, "New Message");
                    }
#if defined(M5STACK_UNITC6L)
                    screen->setOn(true);
                    screen->showSimpleBanner(banner, 1500);
                    if (config.device.buzzer_mode != meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY ||
                        (isAlert && moduleConfig.external_notification.alert_bell_buzzer) ||
                        (!isBroadcast(packet->to) && isToUs(packet))) {
                        // Beep if not in DIRECT_MSG_ONLY mode or if in DIRECT_MSG_ONLY mode and either
                        // - packet contains an alert and alert bell buzzer is enabled
                        // - packet is a non-broadcast that is addressed to this node
                        playLongBeep();
                    }
#else
                    screen->showSimpleBanner(banner, 3000);
#endif
                }
            }
        }
    }

    return 0;
}

// Triggered by MeshModules
int Screen::handleUIFrameEvent(const UIFrameEvent *event)
{
    // Block UI frame events when virtual keyboard is active
    if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
        return 0;
    }

    if (showingNormalScreen) {
        // Regenerate the frameset, potentially honoring a module's internal requestFocus() call
        if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET)
            setFrames(FOCUS_MODULE);

        // Regenerate the frameset, while Attempt to maintain focus on the current frame
        else if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND)
            setFrames(FOCUS_PRESERVE);

        // Don't regenerate the frameset, just re-draw whatever is on screen ASAP
        else if (event->action == UIFrameEvent::Action::REDRAW_ONLY)
            setFastFramerate();
    }

    return 0;
}

void Screen::drawBattMeterFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    auto *self = static_cast<Screen *>(state->userData);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x + display->getWidth() / 2, y, "BATTERY METER");

    if (!self->ensureStingrayMeshReady(false)) {
        display->setFont(FONT_SMALL);
        display->drawString(x + display->getWidth() / 2,
                            y + display->getHeight() / 2 - FONT_HEIGHT_SMALL,
                            "Battery mesh offline");
        return;
    }

    int percent = (battMeterClient && battMeterClient->hasReading()) ? battMeterClient->getLastPercent() : -1;
    float voltage = (battMeterClient && battMeterClient->hasReading()) ? battMeterClient->getLastVoltage() : 0.0f;

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    if (percent < 0) {
        display->drawString(x + display->getWidth() / 2,
                            y + display->getHeight() / 2 - FONT_HEIGHT_SMALL,
                            "Waiting for telemetry...");
        return;
    }

    ClockRenderer::SegmentedDisplayMetrics metrics = ClockRenderer::getSegmentedDisplayMetrics(display);
    float scale = metrics.scale;
    float segmentWidth = metrics.segmentWidth;
    float segmentHeight = metrics.segmentHeight;
    uint16_t digitWidth = segmentWidth + (segmentHeight * 2) + 4;
    uint16_t digitHeight = (segmentWidth * 2) + (segmentHeight * 3) + 8;
    const uint16_t spacing = 5;

    char percentDigits[4];
    snprintf(percentDigits, sizeof(percentDigits), "%d", percent);
    size_t digitsLen = strlen(percentDigits);
    uint16_t percentSymbolWidth = digitWidth;
    uint16_t totalWidth = (digitsLen * digitWidth) + ((digitsLen - 1) * spacing) + spacing + percentSymbolWidth;

    int16_t contentTop = y + FONT_HEIGHT_MEDIUM + 4;
    int16_t contentBottom = y + display->getHeight() - FONT_HEIGHT_SMALL - 4;
    int16_t availableHeight = contentBottom - contentTop;
    int16_t percentY = contentTop;
    if (availableHeight > static_cast<int16_t>(digitHeight)) {
        percentY += (availableHeight - digitHeight) / 2;
    } else {
        percentY = y + (display->getHeight() - digitHeight) / 2;
    }
    int16_t percentX = x + (display->getWidth() - totalWidth) / 2;

    for (size_t i = 0; i < digitsLen; i++) {
        ClockRenderer::drawSegmentedDisplayCharacter(display, percentX, percentY, percentDigits[i] - '0', scale);
        percentX += digitWidth + spacing;
    }

    auto drawPercentSymbol = [&](int16_t symbolX, int16_t symbolY) {
        uint16_t dotSize = static_cast<uint16_t>(segmentHeight);
        if (dotSize == 0) {
            dotSize = 1;
        }
        uint16_t padding = dotSize / 2;
        if (padding == 0) {
            padding = 1;
        }

        display->fillRect(symbolX + padding, symbolY + padding, dotSize, dotSize);
        display->fillRect(symbolX + digitWidth - dotSize - padding, symbolY + digitHeight - dotSize - padding, dotSize, dotSize);

        int lineStartX = symbolX + padding;
        int lineStartY = symbolY + digitHeight - padding - 1;
        int lineEndX = symbolX + digitWidth - padding - 1;
        int lineEndY = symbolY + padding;
        int lineThickness = std::max(1, static_cast<int>(segmentHeight / 2));
        for (int t = -lineThickness / 2; t <= lineThickness / 2; ++t) {
            display->drawLine(lineStartX, lineStartY - t, lineEndX, lineEndY - t);
        }
    };

    drawPercentSymbol(percentX, percentY);

    display->setFont(FONT_SMALL);
    char statusLine[48];
    uint32_t ageSecs = battMeterClient ? (millis() - battMeterClient->getLastUpdateMs()) / 1000 : 0;
    snprintf(statusLine, sizeof(statusLine), "%.2f V   Updated %lus ago", voltage, static_cast<unsigned long>(ageSecs));
    display->drawString(x + display->getWidth() / 2,
                        y + display->getHeight() - FONT_HEIGHT_SMALL - 4,
                        statusLine);
}

void Screen::drawToneGeneratorFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    auto *self = static_cast<Screen *>(state->userData);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x + display->getWidth() / 2, y, "TONE GENERATOR");

    display->setFont(FONT_LARGE);
    char freqBuf[32];
    snprintf(freqBuf, sizeof(freqBuf), "%lu Hz", static_cast<unsigned long>(self->toneGeneratorFrequencyHz));
    display->drawString(x + display->getWidth() / 2,
                        y + display->getHeight() / 2 - FONT_HEIGHT_LARGE,
                        freqBuf);

    display->setFont(FONT_SMALL);
    const char *playState = self->toneGeneratorPlaying ? "Playing" : "Stopped";
    char statusLine[80];
    snprintf(statusLine, sizeof(statusLine), "%s | Tap=Play/Stop Up/Down=+/-50 Right=Play once Left=Back", playState);
    display->drawString(x + display->getWidth() / 2,
                        y + display->getHeight() - FONT_HEIGHT_SMALL - 2,
                        statusLine);
}

void Screen::drawDbMeterFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    auto *self = static_cast<Screen *>(state->userData);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x + display->getWidth() / 2, y, "DB METER");

    if (!self->dbMeterAvailable) {
        display->setFont(FONT_SMALL);
        display->drawString(x + display->getWidth() / 2,
                            y + display->getHeight() / 2 - FONT_HEIGHT_SMALL,
                            "Mic unavailable on this board");
        return;
    }

    self->updateDbMeterReading();

    display->setFont(FONT_LARGE);
    char line[24];
    snprintf(line, sizeof(line), "%.1f dBFS", static_cast<double>(self->dbMeterDbfs));
    display->drawString(x + display->getWidth() / 2, y + (display->getHeight() / 2) - FONT_HEIGHT_LARGE, line);

    // Bar graph
    display->setFont(FONT_SMALL);
    constexpr float minDb = -60.0f;
    constexpr float maxDb = 0.0f;
    float clamped = std::min(std::max(self->dbMeterDbfs, minDb), maxDb);
    int16_t barWidth = display->getWidth() - 20;
    int16_t filled = static_cast<int16_t>((clamped - minDb) / (maxDb - minDb) * barWidth);
    int16_t barX = x + 10;
    int16_t barY = y + display->getHeight() - 20;
    display->drawRect(barX, barY, barWidth, 10);
    if (filled > 2)
        display->fillRect(barX + 1, barY + 1, filled - 2, 8);

    int16_t minMaxY = barY - FONT_HEIGHT_SMALL - 4;
    if (self->dbMeterMinMaxReady) {
        char minmax[48];
        snprintf(minmax, sizeof(minmax), "Min %.1f  Max %.1f", self->dbMeterMinDbfs, self->dbMeterMaxDbfs);
        display->drawString(x + display->getWidth() / 2, minMaxY, minmax);
    }

    int16_t statusY = barY + 12;
    if (!self->dbMeterStatus.empty()) {
        display->drawString(x + display->getWidth() / 2, statusY, self->dbMeterStatus.c_str());
    } else {
        uint32_t ageMs = millis() - self->dbMeterLastUpdateMs;
        char status[32];
        snprintf(status, sizeof(status), "Updated %lums ago", static_cast<unsigned long>(ageMs));
        display->drawString(x + display->getWidth() / 2, statusY, status);
    }
}

void Screen::drawPactTimerFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    auto *self = static_cast<Screen *>(state->userData);
    const uint32_t now = millis();
    const bool recording = self->pactTimerRecording;
    const bool arming = self->pactTimerArming;
    const bool beeping = self->pactTimerBeepActive;
    const bool hasShots = !self->pactShotTimesMs.empty();
    const size_t shotCount = self->pactShotTimesMs.size();
    const bool showButton = self->pactTimerStartVisible && !(arming || beeping || recording);

    int16_t width = display->getWidth();
    int16_t height = display->getHeight();

    display->setColor(BLACK);
    display->fillRect(x, y, width, height);
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    display->setFont(FONT_MEDIUM);
    display->drawString(x + width / 2, y, "PACT TIMER");

    display->setFont(FONT_SMALL);
    char statusLine[64];
    if (arming) {
        uint32_t remaining = (self->pactTimerArmingStartMs + pactCountdownMs > now) ?
                                 (self->pactTimerArmingStartMs + pactCountdownMs - now) :
                                 0;
        uint32_t remSec = (remaining + 999) / 1000;
        if (remSec == 0)
            remSec = 1;
        snprintf(statusLine, sizeof(statusLine), "Standby... %lus", static_cast<unsigned long>(remSec));
    } else if (beeping) {
        snprintf(statusLine, sizeof(statusLine), "Start");
    } else if (recording) {
        float elapsed = static_cast<float>(now - self->pactTimerStringStartMs) / 1000.0f;
        const char *recordStatus = self->pactTimerStatus.empty() ? "Time" : self->pactTimerStatus.c_str();
        snprintf(statusLine, sizeof(statusLine), "%s %.2fs", recordStatus, elapsed);
    } else if (self->pactTimerHasResult && hasShots) {
        float total = static_cast<float>(self->pactTimerTotalMs) / 1000.0f;
        snprintf(statusLine, sizeof(statusLine), "Last Volly: %.2fs (%u)", total, static_cast<unsigned>(shotCount));
    } else if (!self->pactTimerAvailable) {
        snprintf(statusLine, sizeof(statusLine), "Mic unavailable - manual mode");
    } else if (!self->pactTimerStatus.empty()) {
        snprintf(statusLine, sizeof(statusLine), "%s", self->pactTimerStatus.c_str());
    } else {
        snprintf(statusLine, sizeof(statusLine), "Tap start for 3s delay");
    }
    int16_t statusY = y + FONT_HEIGHT_SMALL + 4;
    display->drawString(x + width / 2, statusY, statusLine);

    int16_t contentTop = statusY + FONT_HEIGHT_SMALL + 6;

    if (showButton) {
        int16_t buttonWidth = width - 24;
        int16_t buttonHeight = height / 2;
        int16_t buttonX = x + (width - buttonWidth) / 2;
        int16_t buttonY = contentTop;
        if (buttonY + buttonHeight > (y + height - (FONT_HEIGHT_SMALL + 20)))
            buttonY = y + (height - buttonHeight) / 2;

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawRect(buttonX, buttonY, buttonWidth, buttonHeight);
        display->setFont(FONT_LARGE);
        display->drawString(buttonX + buttonWidth / 2,
                            buttonY + buttonHeight / 2 - FONT_HEIGHT_LARGE / 2,
                            "START");
        display->setFont(FONT_SMALL);
        display->drawString(buttonX + buttonWidth / 2,
                            buttonY + buttonHeight / 2 + FONT_HEIGHT_SMALL,
                            "3s delay");

        contentTop = buttonY + buttonHeight + 6;
    }

    display->setFont(FONT_SMALL);
    char summary[64];
    if (recording) {
        float elapsed = static_cast<float>(now - self->pactTimerStringStartMs) / 1000.0f;
        snprintf(summary, sizeof(summary), "Time %.2fs  Shots %u", elapsed, static_cast<unsigned>(shotCount));
    } else if (self->pactTimerHasResult && hasShots) {
        float total = static_cast<float>(self->pactTimerTotalMs) / 1000.0f;
        snprintf(summary, sizeof(summary), "Total %.2fs  Shots %u", total, static_cast<unsigned>(shotCount));
    } else {
        summary[0] = '\0';
    }

    if (summary[0]) {
        display->drawString(x + width / 2, contentTop, summary);
        contentTop += FONT_HEIGHT_SMALL + 2;
    }

    if (hasShots) {
        size_t startIdx = self->pactTimerSplitOffset;
        if (startIdx >= shotCount)
            startIdx = 0;
        size_t endIdx = std::min(startIdx + pactSplitsPerPage, shotCount);

        for (size_t i = startIdx; i < endIdx; ++i) {
            float shotSec = static_cast<float>(self->pactShotTimesMs[i]) / 1000.0f;
            char shotLine[32];
            snprintf(shotLine, sizeof(shotLine), "%u: %.2fs", static_cast<unsigned>(i + 1), shotSec);
            display->drawString(x + width / 2, contentTop, shotLine);
            contentTop += FONT_HEIGHT_SMALL + 2;
        }
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Tap=Start/Stop  Left=Manual add  Back=Exit");
}

void Screen::drawSecretMenuFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *self = static_cast<Screen *>(state->userData);
    if (!self)
        return;

    if (self->secretMenuMode == SecretMenuMode::PactTimer) {
        self->drawPactTimerFrame(display, state, x, y);
        return;
    }
    if (self->secretMenuMode == SecretMenuMode::ToneGenerator) {
        self->drawToneGeneratorFrame(display, state, x, y);
        return;
    }
    if (self->secretMenuMode == SecretMenuMode::DbMeter) {
        self->drawDbMeterFrame(display, state, x, y);
        return;
    }

    int16_t width = display->getWidth();
    int16_t height = display->getHeight();
    display->setColor(BLACK);
    display->fillRect(x, y, width, height);
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);

    const char *title = "SECRET MENU";
    if (self->secretMenuMode == SecretMenuMode::WifiAttacks)
        title = "WIFI ATTACKS";
    else if (self->secretMenuMode == SecretMenuMode::WifiScanner)
        title = "WIFI SCANNER";
    else if (self->secretMenuMode == SecretMenuMode::StationAps)
        title = "STATION APs";
    else if (self->secretMenuMode == SecretMenuMode::StationStations)
        title = "STATIONS";
    else if (self->secretMenuMode == SecretMenuMode::Detonate)
        title = "DETONATE";
    else if (self->secretMenuMode == SecretMenuMode::MsdCalculator)
        title = "MSD CALC";

    display->drawString(x + width / 2, y, title);

    int16_t listStartY = y + FONT_HEIGHT_MEDIUM + 6;
    int16_t lineHeight = FONT_HEIGHT_SMALL + 4;
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (self->secretMenuMode == SecretMenuMode::Root) {
        size_t firstIndex = 0;
        if (secretMenuItemCount > wifiAttackVisibleCount && self->secretMenuSelection >= wifiAttackVisibleCount)
            firstIndex = self->secretMenuSelection - (wifiAttackVisibleCount - 1);
        if (firstIndex + wifiAttackVisibleCount > secretMenuItemCount)
            firstIndex = (secretMenuItemCount > wifiAttackVisibleCount) ? secretMenuItemCount - wifiAttackVisibleCount : 0;

        for (size_t i = 0; i < wifiAttackVisibleCount && (firstIndex + i) < secretMenuItemCount; ++i) {
            size_t itemIndex = firstIndex + i;
            int16_t itemY = listStartY + lineHeight * i;
            std::string label = secretMenuItemLabel(itemIndex);
            if (itemIndex == self->secretMenuSelection)
                display->drawString(x + 2, itemY, ">");
            display->drawString(x + 14, itemY, label.c_str());
        }

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Tap=Select  Left=Hide");
        return;
    }

    if (self->secretMenuMode == SecretMenuMode::WifiAttacks) {
        bool attackRunning = false;
        size_t runningIndex = 0;
#if HAS_WIFI && defined(ARCH_ESP32)
        auto &attackCtrl = marauder::WifiAttackController::instance();
        attackRunning = attackCtrl.isRunning();
        if (attackRunning)
            runningIndex = marauder::attackIndex(attackCtrl.currentAttack());
#endif
        size_t firstIndex = 0;
        if (wifiAttackItemCount > wifiAttackVisibleCount && self->wifiAttackSelection >= wifiAttackVisibleCount)
            firstIndex = self->wifiAttackSelection - (wifiAttackVisibleCount - 1);
        if (firstIndex + wifiAttackVisibleCount > wifiAttackItemCount)
            firstIndex = (wifiAttackItemCount > wifiAttackVisibleCount) ? wifiAttackItemCount - wifiAttackVisibleCount : 0;

        for (size_t i = 0; i < wifiAttackVisibleCount && (firstIndex + i) < wifiAttackItemCount; ++i) {
            size_t itemIndex = firstIndex + i;
            int16_t itemY = listStartY + lineHeight * i;
            const char *label = marauder::wifiAttackLabel(itemIndex);
            char lineBuf[72];
            snprintf(lineBuf, sizeof(lineBuf), "%s%s", label, (attackRunning && itemIndex == runningIndex) ? " *" : "");
            if (itemIndex == self->wifiAttackSelection)
                display->drawString(x + 2, itemY, ">");
            display->drawString(x + 14, itemY, lineBuf);
        }

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Tap=Toggle  Left=Back");
        return;
    }

    if (self->secretMenuMode == SecretMenuMode::WifiScanner) {
        if (wifiScanInProgress || wifiScanResults.empty()) {
            const char *status = wifiScanInProgress ? "Scanning..." : wifiScanStatus.c_str();
            display->drawString(x + 2, listStartY, status);
        } else {
            size_t visible = wifiAttackVisibleCount;
            size_t firstIndex = 0;
            if (wifiScanResults.size() > visible && wifiScanSelection >= visible)
                firstIndex = wifiScanSelection - (visible - 1);
            if (firstIndex + visible > wifiScanResults.size())
                firstIndex = (wifiScanResults.size() > visible) ? wifiScanResults.size() - visible : 0;
            for (size_t i = 0; i < visible && (firstIndex + i) < wifiScanResults.size(); ++i) {
                size_t itemIndex = firstIndex + i;
                int16_t itemY = listStartY + lineHeight * i;
                const auto &entry = wifiScanResults[itemIndex];
                const char *ssid = entry.ssid.empty() ? "<hidden>" : entry.ssid.c_str();
                bool preferred = wifiEntryIsPreferred(entry);
                char lineBuf[96];
                snprintf(lineBuf,
                         sizeof(lineBuf),
                         "%s (%ddBm ch%u %s)%s",
                         ssid,
                         static_cast<int>(entry.rssi),
                         entry.channel,
                         wifiAuthLabel(entry.security),
                         preferred ? " *" : "");
                if (itemIndex == wifiScanSelection)
                    display->drawString(x + 2, itemY, ">");
                display->drawString(x + 14, itemY, lineBuf);
            }
        }

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        if (wifiPreferredValid) {
            const std::string targetLine = secretMenuTargetString();
            display->drawString(x + width / 2, y + height - (2 * FONT_HEIGHT_SMALL + 4), targetLine.c_str());
        }
        display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Tap=Rescan  Right=Target  Left=Back");
        return;
    }

    if (self->secretMenuMode == SecretMenuMode::StationAps || self->secretMenuMode == SecretMenuMode::StationStations) {
#if HAS_WIFI && defined(ARCH_ESP32)
        auto &tracker = marauder::StationTracker::instance();
        const auto &aps = tracker.getAccessPoints();
        const auto &stations = tracker.getStations();
        if (self->secretMenuMode == SecretMenuMode::StationAps) {
            if (aps.empty()) {
                display->drawString(x + 2, listStartY, "Listening for APs...");
            } else {
                size_t visible = wifiAttackVisibleCount;
                size_t firstIndex = 0;
                if (aps.size() > visible && stationApSelection >= visible)
                    firstIndex = stationApSelection - (visible - 1);
                if (firstIndex + visible > aps.size())
                    firstIndex = (aps.size() > visible) ? aps.size() - visible : 0;
                for (size_t i = 0; i < visible && (firstIndex + i) < aps.size(); ++i) {
                    size_t itemIndex = firstIndex + i;
                    int16_t itemY = listStartY + lineHeight * i;
                    const auto &ap = aps[itemIndex];
                    const char *ssid = ap.ssid.empty() ? "<hidden>" : ap.ssid.c_str();
                    char lineBuf[96];
                    snprintf(lineBuf, sizeof(lineBuf), "%s (ch%u %u stas)", ssid, ap.channel, static_cast<unsigned>(ap.stationIndices.size()));
                    if (itemIndex == stationApSelection)
                        display->drawString(x + 2, itemY, ">");
                    display->drawString(x + 14, itemY, lineBuf);
                }
            }
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Tap=Open  Left=Back");
        } else {
            if (stationApSelection >= aps.size()) {
                display->drawString(x + 2, listStartY, "No AP selected");
            } else {
                const auto &ap = aps[stationApSelection];
                const auto &indices = ap.stationIndices;
                if (indices.empty()) {
                    display->drawString(x + 2, listStartY, "No stations yet");
                } else {
                    size_t visible = wifiAttackVisibleCount;
                    size_t firstIndex = 0;
                    if (indices.size() > visible && stationStaSelection >= visible)
                        firstIndex = stationStaSelection - (visible - 1);
                    if (firstIndex + visible > indices.size())
                        firstIndex = (indices.size() > visible) ? indices.size() - visible : 0;
                    for (size_t i = 0; i < visible && (firstIndex + i) < indices.size(); ++i) {
                        size_t idx = firstIndex + i;
                        int16_t itemY = listStartY + lineHeight * i;
                        size_t stationIndex = indices[idx];
                        if (stationIndex >= stations.size())
                            continue;
                        const auto &sta = stations[stationIndex];
                        char lineBuf[96];
                        snprintf(lineBuf, sizeof(lineBuf), "%s (%ddBm ch%u)", macToString(sta.mac).c_str(), static_cast<int>(sta.rssi), sta.channel);
                        if (idx == stationStaSelection)
                            display->drawString(x + 2, itemY, ">");
                        display->drawString(x + 14, itemY, lineBuf);
                    }
                }
            }
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Right=Target  Left=Back");
        }
#else
        display->drawString(x + width / 2, y + height / 2, "Station browser unavailable");
#endif
        return;
    }

    if (self->secretMenuMode == SecretMenuMode::Detonate) {
        if (self->detonateModeActive)
            self->updateDetonateStatus();
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_LARGE);
        display->drawString(x + width / 2, y + height / 2 - FONT_HEIGHT_LARGE, "DETONATE");
        display->setFont(FONT_SMALL);
        display->drawString(x + width / 2, y + height / 2 + 2, self->detonateStatus.c_str());
        const std::string targetLine = secretMenuTargetString();
        display->drawString(x + width / 2, y + height - (2 * FONT_HEIGHT_SMALL + 4), targetLine.c_str());
        display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Tap=Detonate  Left=Back");
        return;
    }

    if (self->secretMenuMode == SecretMenuMode::MsdCalculator) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_SMALL);
        display->drawString(x + width / 2, listStartY, "AP started; send data to calculator");
        display->drawString(x + width / 2, listStartY + lineHeight, "Swipe to MSD Summary frame");
        display->drawString(x + width / 2, y + height - (FONT_HEIGHT_SMALL + 2), "Left=Exit");
        return;
    }
}

void Screen::drawMsdStatusFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *self = static_cast<Screen *>(state->userData);
    if (!self)
        return;

    const auto &results = self->msdCalculatorServer.getResults();
    display->setColor(BLACK);
    display->fillRect(x, y, display->getWidth(), display->getHeight());
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    display->setFont(FONT_MEDIUM);
    display->drawString(x + display->getWidth() / 2, y, "MSD Calculator");

    int16_t cursorY = y + FONT_HEIGHT_MEDIUM + 6;
    display->setFont(FONT_SMALL);
    display->drawString(x + display->getWidth() / 2, cursorY, self->msdCalculatorServer.statusText());
    cursorY += FONT_HEIGHT_SMALL + 4;

#if HAS_WIFI
    if (!results.valid) {
        String ipAddress = WiFi.softAPIP().toString();
        std::string ipLine = std::string("AP: ") + ipAddress.c_str();
        display->drawString(x + display->getWidth() / 2, cursorY, ipLine.c_str());
        cursorY += FONT_HEIGHT_SMALL + 4;
    }
#else
    if (!results.valid) {
        display->drawString(x + display->getWidth() / 2, cursorY, "WiFi unavailable");
        cursorY += FONT_HEIGHT_SMALL + 4;
    }
#endif

    display->setFont(FONT_LARGE);
    int16_t resultY = cursorY + 4;
    int16_t lineStep = FONT_HEIGHT_LARGE + 8;
    char lineBuf[32];
    if (results.valid) {
        snprintf(lineBuf, sizeof(lineBuf), "TNT %.3f lbs", results.tnt);
        display->drawString(x + display->getWidth() / 2, resultY, lineBuf);
        snprintf(lineBuf, sizeof(lineBuf), "K18 %.1f ft", results.msd18);
        display->drawString(x + display->getWidth() / 2, resultY + lineStep, lineBuf);
        snprintf(lineBuf, sizeof(lineBuf), "K24 %.1f ft", results.msd24);
        display->drawString(x + display->getWidth() / 2, resultY + 2 * lineStep, lineBuf);
    } else {
        display->drawString(x + display->getWidth() / 2, resultY, "Waiting for data");
    }
}

void Screen::showSecretMenu()
{
    if (secretMenuVisible)
        return;
    secretMenuVisible = true;
    secretMenuSelection = 0;
    wifiAttackSelection = 0;
    wifiScanSelection = 0;
    wifiScanResults.clear();
    wifiScanStatus = "Tap to scan";
    wifiScanInProgress = false;
    stationApSelection = 0;
    stationStaSelection = 0;
    secretMenuMode = SecretMenuMode::Root;
    secretGestureProgress = 0;
    LOG_INFO("Secret menu unlocked (frame count=%u)", framesetInfo.frameCount);
    setFrames(FOCUS_SECRET);
}

void Screen::hideSecretMenu()
{
    if (!secretMenuVisible)
        return;
    secretMenuVisible = false;
    if (secretMenuMode == SecretMenuMode::Detonate)
        exitDetonateMode();
    if (secretMenuMode == SecretMenuMode::ToneGenerator)
        stopToneGeneratorMode();
    if (secretMenuMode == SecretMenuMode::MsdCalculator)
        stopMsdCalculatorMode();
    if (secretMenuMode == SecretMenuMode::DbMeter)
        stopDbMeterMode();
    if (secretMenuMode == SecretMenuMode::PactTimer)
        stopPactTimerMode();
    secretMenuMode = SecretMenuMode::Root;
    wifiAttackSelection = 0;
    wifiScanInProgress = false;
    stationApSelection = 0;
    stationStaSelection = 0;
#if HAS_WIFI && defined(ARCH_ESP32)
    marauder::StationTracker::instance().stop();
#endif
    LOG_INFO("Secret menu hidden");
    setFrames(FOCUS_PRESERVE);
}

void Screen::handleSecretMenuSelection()
{
    if (secretMenuMode == SecretMenuMode::Root) {
        SecretMenuEntry entry = selectedSecretMenuEntry(secretMenuSelection);
        switch (entry) {
        case SecretMenuEntry::TvBGone:
            if (tvBGone && tvBGone->isActive()) {
                stopTvBGoneTool();
            } else {
                startTvBGoneTool();
                hideSecretMenu();
            }
            break;
        case SecretMenuEntry::WifiAttacks:
            secretMenuMode = SecretMenuMode::WifiAttacks;
            wifiAttackSelection = 0;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::WifiScanner:
            secretMenuMode = SecretMenuMode::WifiScanner;
            wifiScanSelection = 0;
            startWifiScanList();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::Detonate:
            enterDetonateMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::ToneGenerator:
            startToneGeneratorMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::DbMeter:
            startDbMeterMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::PactTimer:
            startPactTimerMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::MsdCalculator:
            startMsdCalculatorMode();
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            break;
        case SecretMenuEntry::StationBrowser:
            showStationBrowser();
            break;
        case SecretMenuEntry::BatteryMeter:
            startBattMeterMode();
            hideSecretMenu();
            break;
        default:
            LOG_INFO("Toolkit entry \"%s\" not implemented yet", secretMenuItemName(entry));
            break;
        }
    } else if (secretMenuMode == SecretMenuMode::WifiAttacks) {
        startWiFiAttackTool(wifiAttackSelection);
    }
}

void Screen::startWifiScanList()
{
#if HAS_WIFI
    if (wifiScanInProgress)
        return;
    wifiScanInProgress = true;
    wifiScanStatus = "Scanning...";
    wifiScanResults.clear();
    setFastFramerate();
    int16_t count = WiFi.scanNetworks(false, true);
    wifiScanInProgress = false;
    if (count <= 0) {
        wifiScanStatus = "No networks found";
        WiFi.scanDelete();
        return;
    }
    wifiScanResults.reserve(count);
    for (int16_t i = 0; i < count; ++i) {
        SecretWifiScanResult entry;
        entry.ssid = WiFi.SSID(i).c_str();
        entry.rssi = WiFi.RSSI(i);
        entry.channel = WiFi.channel(i);
#if HAS_WIFI && defined(ARCH_ESP32)
        entry.security = static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i));
#else
        entry.security = 0;
#endif
        if (auto *bssid = WiFi.BSSID(i)) {
            std::copy(bssid, bssid + entry.bssid.size(), entry.bssid.begin());
        }
        wifiScanResults.push_back(std::move(entry));
    }
    WiFi.scanDelete();
    wifiScanSelection = 0;
    char statusBuf[32];
    snprintf(statusBuf, sizeof(statusBuf), "Found %d network%s", count, (count == 1) ? "" : "s");
    wifiScanStatus = statusBuf;
#else
    wifiScanInProgress = false;
    wifiScanStatus = "WiFi unavailable";
#endif
}

void Screen::showStationBrowser()
{
#if HAS_WIFI && defined(ARCH_ESP32)
    auto &tracker = marauder::StationTracker::instance();
    tracker.start();
    stationApSelection = 0;
    stationStaSelection = 0;
    secretMenuMode = SecretMenuMode::StationAps;
    setFastFramerate();
    setFrames(FOCUS_SECRET);
#else
    LOG_WARN("Station browser unavailable on this hardware");
#endif
}

void Screen::startBattMeterMode()
{
    if (detonateModeActive) {
        LOG_WARN("Ignoring battery meter request while detonate mode is active");
        return;
    }

    if (battMeterClient && battMeterClient->isActive())
        battMeterClient->stop();

    if (!ensureStingrayMeshReady(false)) {
        LOG_WARN("Batt meter client unavailable");
        battMeterActive = false;
        return;
    }
    battMeterActive = true;
    battMeterUnlocked = true;
    setFrames(FOCUS_BATTMETER);
}

void Screen::stopBattMeterMode()
{
    if (!battMeterActive)
        return;
    battMeterActive = false;
    if (battMeterClient && battMeterClient->isActive())
        battMeterClient->stop();
    setFrames(FOCUS_PRESERVE);
}

void Screen::startToneGeneratorMode()
{
    if (detonateModeActive)
        exitDetonateMode();
    if (battMeterActive)
        stopBattMeterMode();

    toneGeneratorActive = true;
    toneGeneratorPlaying = false;
    toneGeneratorFrequencyHz = 2600;
    stopTonePlayback();
    secretMenuMode = SecretMenuMode::ToneGenerator;
    setFrames(FOCUS_SECRET);
}

void Screen::stopToneGeneratorMode()
{
    stopTonePlayback();
    toneGeneratorActive = false;
    secretMenuMode = SecretMenuMode::Root;
}

void Screen::playToneOnce()
{
    if (!toneGeneratorActive)
        return;
    audio::toneOutputPlay(toneGeneratorFrequencyHz);
    toneGeneratorPlaying = true;
}

void Screen::stopTonePlayback()
{
    audio::toneOutputStop();
    toneGeneratorPlaying = false;
}

void Screen::toggleTonePlayback()
{
    if (!toneGeneratorActive)
        return;
    if (toneGeneratorPlaying)
        stopTonePlayback();
    else
        playToneOnce();
}

void Screen::updateDbMeterReading()
{
#if defined(ARCH_ESP32) && (dbMicClkPin >= 0) && (dbMicDataPin >= 0)
    if (!dbMeterActive)
        return;

    static int16_t samples[256];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, 5 / portTICK_PERIOD_MS);
    if (err != ESP_OK || bytesRead == 0) {
        dbMeterStatus = "Mic read error";
        return;
    }

    const size_t count = bytesRead / sizeof(int16_t);
    double accum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double s = samples[i];
        accum += s * s;
    }
    double rms = sqrt(accum / static_cast<double>(count));
    double norm = rms / 32768.0;
    if (norm < 1e-9)
        norm = 1e-9;
    dbMeterDbfs = static_cast<float>(20.0 * log10(norm));
    dbMeterLastUpdateMs = millis();
    dbMeterStatus.clear();
    if (!dbMeterMinMaxReady) {
        dbMeterMinDbfs = dbMeterDbfs;
        dbMeterMaxDbfs = dbMeterDbfs;
        dbMeterMinMaxReady = true;
    } else {
        dbMeterMinDbfs = std::min(dbMeterMinDbfs, dbMeterDbfs);
        dbMeterMaxDbfs = std::max(dbMeterMaxDbfs, dbMeterDbfs);
    }
#endif
}

void Screen::startDbMeterMode()
{
    if (detonateModeActive)
        exitDetonateMode();
    if (battMeterActive)
        stopBattMeterMode();
    if (toneGeneratorActive)
        stopToneGeneratorMode();

    dbMeterMinMaxReady = false;
    dbMeterMinDbfs = dbMeterMaxDbfs = dbMeterDbfs = -90.0f;
    dbMeterStatus = "Listening...";

#if defined(ARCH_ESP32) && (dbMicClkPin >= 0) && (dbMicDataPin >= 0)
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = dbMicClkPin;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = dbMicDataPin;

    if (dbMeterActive)
        i2s_driver_uninstall(I2S_NUM_0);

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        dbMeterStatus = "Mic init failed";
        dbMeterActive = false;
        dbMeterAvailable = false;
        secretMenuMode = SecretMenuMode::DbMeter;
        setFrames(FOCUS_SECRET);
        return;
    }
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK ||
        i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO) != ESP_OK) {
        dbMeterStatus = "Mic pin/clks failed";
        i2s_driver_uninstall(I2S_NUM_0);
        dbMeterActive = false;
        dbMeterAvailable = false;
        secretMenuMode = SecretMenuMode::DbMeter;
        setFrames(FOCUS_SECRET);
        return;
    }
    dbMeterActive = true;
    dbMeterAvailable = true;
    dbMeterStatus.clear();
    dbMeterDbfs = -90.0f;
    dbMeterLastUpdateMs = millis();
    secretMenuMode = SecretMenuMode::DbMeter;
    setFrames(FOCUS_SECRET);
#else
    dbMeterActive = false;
    dbMeterAvailable = false;
    dbMeterStatus = "Mic unavailable";
    secretMenuMode = SecretMenuMode::DbMeter;
    setFrames(FOCUS_SECRET);
#endif
}

void Screen::stopDbMeterMode()
{
#if defined(ARCH_ESP32)
    if (dbMeterActive)
        i2s_driver_uninstall(I2S_NUM_0);
#endif
    dbMeterActive = false;
    secretMenuMode = SecretMenuMode::Root;
}

void Screen::startPactTimerMode()
{
    if (detonateModeActive)
        exitDetonateMode();
    if (battMeterActive)
        stopBattMeterMode();
    if (toneGeneratorActive)
        stopToneGeneratorMode();
    if (dbMeterActive)
        stopDbMeterMode();

    pactTimerSplitOffset = 0;
    pactTimerArming = false;
    pactTimerBeepActive = false;
    pactTimerRecording = false;
    pactTimerMicReady = false;
    pactTimerLastPeakDbfs = -90.0f;
    pactTimerStartVisible = true;
    if (!pactTimerHasResult)
        pactTimerStatus.clear();
    secretMenuMode = SecretMenuMode::PactTimer;
}

void Screen::stopPactTimerMode(bool clearResult)
{
    audio::toneOutputStop();
    audio::toneOutputReset();
    stopPactMic();
    pactTimerArming = false;
    pactTimerBeepActive = false;
    if (pactTimerRecording && !pactShotTimesMs.empty())
        pactTimerTotalMs = pactShotTimesMs.back();
    pactTimerRecording = false;
    pactTimerMicReady = false;
    pactTimerSplitOffset = 0;
    pactTimerStartVisible = false;
    if (clearResult) {
        pactShotTimesMs.clear();
        pactTimerHasResult = false;
        pactTimerTotalMs = 0;
        pactTimerStatus.clear();
    } else if (!pactShotTimesMs.empty()) {
        pactTimerHasResult = true;
        pactTimerTotalMs = pactShotTimesMs.back();
    } else {
        pactTimerTotalMs = 0;
    }
    pactTimerStartVisible = !pactTimerHasResult;
    if (pactShotTimesMs.empty() && !pactTimerHasResult && !clearResult)
        pactTimerStatus = "No shots captured";
}

void Screen::beginPactCountdown()
{
    audio::toneOutputStop();
    audio::toneOutputReset();
    stopPactMic();
    pactShotTimesMs.clear();
    pactTimerSplitOffset = 0;
    pactTimerTotalMs = 0;
    pactTimerHasResult = false;
    pactTimerArming = true;
    pactTimerBeepActive = false;
    pactTimerRecording = false;
    pactTimerMicReady = false;
    pactTimerStartVisible = !pactTimerHasResult;
    pactTimerArmingStartMs = millis();
    pactTimerLastShotAbsMs = 0;
    pactTimerStatus = pactTimerAvailable ? "Standby..." : "Manual mode (Right adds)";
}

void Screen::finalizePactString()
{
    audio::toneOutputStop();
    stopPactMic();
    pactTimerTotalMs = pactShotTimesMs.empty() ? 0 : pactShotTimesMs.back();
    pactTimerArming = false;
    pactTimerBeepActive = false;
    pactTimerRecording = false;
    pactTimerMicReady = false;
    pactTimerHasResult = !pactShotTimesMs.empty();
    pactTimerStartVisible = !pactTimerHasResult;
    if (pactTimerHasResult)
        pactTimerStatus = "String saved";
    else
        pactTimerStatus = "No shots captured";
}

void Screen::addPactShotManual()
{
    if (!pactTimerRecording)
        return;
    uint32_t now = millis();
    if (pactTimerLastShotAbsMs != 0 && (now - pactTimerLastShotAbsMs) < pactShotMinGapMs)
        return;
    uint32_t shotMs = now - pactTimerStringStartMs;
    pactShotTimesMs.push_back(shotMs);
    pactTimerLastShotAbsMs = now;
    pactTimerHasResult = true;
    pactTimerStatus = "Manual shot added";
    if (pactShotTimesMs.size() > pactSplitsPerPage)
        pactTimerSplitOffset = pactShotTimesMs.size() - pactSplitsPerPage;
}

bool Screen::ensurePactMicReady()
{
#if defined(ARCH_ESP32) && (dbMicClkPin >= 0) && (dbMicDataPin >= 0)
    if (!pactTimerAvailable)
        return false;
    if (pactTimerMicReady)
        return true;

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = dbMicClkPin;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = dbMicDataPin;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        pactTimerStatus = "Mic init failed";
        pactTimerMicReady = false;
        pactTimerAvailable = false;
        return false;
    }
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK ||
        i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO) != ESP_OK) {
        pactTimerStatus = "Mic pin/clks failed";
        i2s_driver_uninstall(I2S_NUM_0);
        pactTimerMicReady = false;
        pactTimerAvailable = false;
        return false;
    }
    pactTimerMicReady = true;
    pactTimerAvailable = true;
    return true;
#else
    pactTimerAvailable = false;
    return false;
#endif
}

void Screen::stopPactMic()
{
#if defined(ARCH_ESP32)
    if (pactTimerMicReady)
        i2s_driver_uninstall(I2S_NUM_0);
#endif
    pactTimerMicReady = false;
    audio::toneOutputReset();
}

void Screen::updatePactTimer()
{
    if (!(pactTimerArming || pactTimerBeepActive || pactTimerRecording))
        return;

    const uint32_t now = millis();

    if (pactTimerArming && !pactTimerBeepActive) {
        if ((now - pactTimerArmingStartMs) >= pactCountdownMs) {
            pactTimerArming = false;
            pactTimerBeepActive = true;
            pactTimerBeepEndMs = now + pactBeepDurationMs;
            pactTimerStatus = "Start";
            stopPactMic();
            audio::toneOutputStop();
            audio::toneOutputReset();
            delay(15);
            audio::toneOutputPlay(3500);
        }
    }

    if (pactTimerBeepActive && now >= pactTimerBeepEndMs) {
        audio::toneOutputStop();
        audio::toneOutputReset();
        pactTimerBeepActive = false;
        pactTimerRecording = true;
        pactTimerStringStartMs = now;
        pactTimerLastShotAbsMs = 0;
        pactTimerStatus = pactTimerAvailable ? "Time" : "Manual mode (Right adds)";
        ensurePactMicReady();
    }

    if (!pactTimerRecording)
        return;

#if defined(ARCH_ESP32) && (dbMicClkPin >= 0) && (dbMicDataPin >= 0)
    if (!pactTimerAvailable)
        return;
    if (!ensurePactMicReady())
        return;

    static int16_t samples[256];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, 5 / portTICK_PERIOD_MS);
    if (err != ESP_OK || bytesRead == 0)
        return;

    size_t count = bytesRead / sizeof(int16_t);
    int16_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int16_t s = samples[i];
        if (s < 0)
            s = -s;
        if (s > peak)
            peak = s;
    }
    float norm = static_cast<float>(peak) / 32768.0f;
    if (norm < 1e-6f)
        norm = 1e-6f;
    pactTimerLastPeakDbfs = 20.0f * log10f(norm);

    if (pactTimerLastPeakDbfs >= pactShotThresholdDbfs) {
        if (pactTimerLastShotAbsMs == 0 || (now - pactTimerLastShotAbsMs) >= pactShotMinGapMs) {
            uint32_t shotMs = now - pactTimerStringStartMs;
            pactShotTimesMs.push_back(shotMs);
            pactTimerLastShotAbsMs = now;
            pactTimerHasResult = true;
            pactTimerStatus = "Shot detected";
            if (pactShotTimesMs.size() > pactSplitsPerPage)
                pactTimerSplitOffset = pactShotTimesMs.size() - pactSplitsPerPage;
            setFastFramerate();
        }
    }
#else
    (void)now;
#endif
}

void Screen::startMsdCalculatorMode()
{
#if HAS_WIFI
    if (detonateModeActive)
        exitDetonateMode();
    if (battMeterActive)
        stopBattMeterMode();
    if (toneGeneratorActive)
        stopToneGeneratorMode();
    msdSummaryActive = true;
    msdCalculatorServer.start();
    secretMenuMode = SecretMenuMode::MsdCalculator;
#else
    LOG_WARN("MSD Calculator requires WiFi");
#endif
}

void Screen::stopMsdCalculatorMode()
{
#if HAS_WIFI
    msdCalculatorServer.stop();
#endif
    secretMenuMode = SecretMenuMode::Root;
}

void Screen::startWiFiAttackTool(size_t attackIndex)
{
    if (attackIndex >= wifiAttackItemCount)
        return;
#if HAS_WIFI && defined(ARCH_ESP32)
    auto type = marauder::attackTypeFromIndex(attackIndex);
    bool started = marauder::WifiAttackController::instance().toggle(type);
    LOG_INFO("WiFi attack \"%s\" %s",
             marauder::wifiAttackLabel(type),
             started ? "started" : "stopped");
#else
    LOG_WARN("WiFi attack \"%s\" unavailable on this hardware", marauder::wifiAttackLabel(attackIndex));
#endif
}

void Screen::startTvBGoneTool()
{
#if defined(ARDUINO_ARCH_ESP32)
    if (tvBGone)
        tvBGone->start();
#endif
}

void Screen::stopTvBGoneTool()
{
#if defined(ARDUINO_ARCH_ESP32)
    if (tvBGone)
        tvBGone->stop();
#endif
}

bool Screen::ensureStingrayMeshReady(bool longRange)
{
#if HAS_WIFI
    if (!battMeterClient)
        return false;

    LOG_INFO("ensureStingrayMeshReady: requested longRange=%s, clientActive=%s, clientLR=%s, meshReady=%s",
             longRange ? "true" : "false",
             battMeterClient->isActive() ? "true" : "false",
             battMeterClient->isLongRangeMode() ? "true" : "false",
             battMeterClient->isMeshReady() ? "true" : "false");

    bool needsRestart = !battMeterClient->isActive() ||
                        battMeterClient->isLongRangeMode() != longRange;
    if (needsRestart || !battMeterClient->isMeshReady()) {
        battMeterClient->start(longRange);
        LOG_INFO("ensureStingrayMeshReady: restarted mesh with longRange=%s", longRange ? "true" : "false");
    }
    return battMeterClient->isMeshReady();
#else
    (void)longRange;
    return false;
#endif
}

void Screen::enterDetonateMode()
{
#if HAS_WIFI
    if (detonateModeActive)
        return;
    detonateModeActive = true;
    bool meshWasRunning = battMeterClient && battMeterClient->isActive();
    if (battMeterActive) {
        battNetworkWasActive = true;
        stopBattMeterMode();
    } else {
        battNetworkWasActive = false;
        if (meshWasRunning && battMeterClient)
            battMeterClient->stop();
    }
    detonateConnected = false;
    detonateStatus = "Starting claymore link...";
    if (!ensureStingrayMeshReady(true)) {
        detonateStatus = "WiFi not available";
        detonateModeActive = false;
        return;
    }

    if (battMeterClient) {
        bool sent = battMeterClient->sendMeshCommand("DETONATE", "NONE");
        LOG_INFO("Detonate handshake sent (DETONATE=NONE), sent=%s", sent ? "true" : "false");
    }

    secretMenuMode = SecretMenuMode::Detonate;
#else
    detonateStatus = "WiFi unavailable";
    secretMenuMode = SecretMenuMode::Detonate;
#endif
}

void Screen::exitDetonateMode()
{
#if HAS_WIFI
    detonateModeActive = false;
    detonateConnected = false;
    detonateStatus = "Tap detonate to broadcast";
    if (battMeterClient && battMeterClient->isActive())
        battMeterClient->stop();
    if (battNetworkWasActive)
        startBattMeterMode();
#endif
    secretMenuMode = SecretMenuMode::Root;
}

void Screen::updateDetonateStatus()
{
#if HAS_WIFI
    if (!detonateModeActive || !battMeterClient)
        return;
    detonateConnected = battMeterClient->hasMeshPeers();
    if (detonateConnected)
        detonateStatus = "Claymore connected";
    else
        detonateStatus = "Searching for claymore...";
#endif
}

bool Screen::sendDetonateCommand()
{
#if HAS_WIFI
    if (!ensureStingrayMeshReady(true)) {
        detonateStatus = "Not connected to claymore";
        return false;
    }
    bool sent = battMeterClient->sendMeshCommand("DETONATE", "detonate");
    detonateStatus = sent ? "Detonate command sent" : "Failed to send detonate";
    return sent;
#else
    detonateStatus = "WiFi not available";
    return false;
#endif
}

bool Screen::handleSecretMenuInput(uint8_t inputEvent)
{
    if (!secretMenuVisible)
        return false;

    auto handleWifiAttackNav = [this](uint8_t evt) -> bool {
        switch (evt) {
        case INPUT_BROKER_UP:
            if (wifiAttackItemCount > 0)
                wifiAttackSelection = (wifiAttackSelection + wifiAttackItemCount - 1) % wifiAttackItemCount;
            setFastFramerate();
            return true;
        case INPUT_BROKER_DOWN:
            if (wifiAttackItemCount > 0)
                wifiAttackSelection = (wifiAttackSelection + 1) % wifiAttackItemCount;
            setFastFramerate();
            return true;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
            LOG_INFO("WiFi attack menu selection: %s", marauder::wifiAttackLabel(wifiAttackSelection));
            handleSecretMenuSelection();
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_RIGHT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            secretMenuMode = SecretMenuMode::Root;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            return true;
        default:
            return true;
        }
    };

    auto handleWifiScanNav = [this](uint8_t evt) -> bool {
        switch (evt) {
        case INPUT_BROKER_UP:
            if (!wifiScanResults.empty())
                wifiScanSelection = (wifiScanSelection + wifiScanResults.size() - 1) % wifiScanResults.size();
            setFastFramerate();
            return true;
        case INPUT_BROKER_DOWN:
            if (!wifiScanResults.empty())
                wifiScanSelection = (wifiScanSelection + 1) % wifiScanResults.size();
            setFastFramerate();
            return true;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
            startWifiScanList();
            setFastFramerate();
            return true;
        case INPUT_BROKER_RIGHT:
            if (!wifiScanResults.empty()) {
                const auto &entry = wifiScanResults[wifiScanSelection];
                wifiPreferredValid = true;
                wifiPreferredBssid = entry.bssid;
                wifiPreferredSsid = entry.ssid;
                wifiScanStatus = std::string("Selected: ") + (entry.ssid.empty() ? "<hidden>" : entry.ssid.c_str());
#if HAS_WIFI && defined(ARCH_ESP32)
                marauder::WifiAttackController::instance().setPreferredTarget(entry.bssid, entry.channel);
#endif
                LOG_INFO("WiFi scanner selected %s (ch%u)",
                         entry.ssid.empty() ? "<hidden>" : entry.ssid.c_str(),
                         entry.channel);
                secretMenuMode = SecretMenuMode::Root;
                setFrames(FOCUS_SECRET);
            }
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            secretMenuMode = SecretMenuMode::Root;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            return true;
        default:
            return true;
        }
    };

    auto handleStationApNav = [this](uint8_t evt) -> bool {
#if HAS_WIFI && defined(ARCH_ESP32)
        auto &tracker = marauder::StationTracker::instance();
        const auto &aps = tracker.getAccessPoints();
        size_t total = aps.size();
        switch (evt) {
        case INPUT_BROKER_UP:
            if (total > 0)
                stationApSelection = (stationApSelection + total - 1) % total;
            setFastFramerate();
            return true;
        case INPUT_BROKER_DOWN:
            if (total > 0)
                stationApSelection = (stationApSelection + 1) % total;
            setFastFramerate();
            return true;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
        case INPUT_BROKER_RIGHT:
            if (total > 0) {
                stationStaSelection = 0;
                secretMenuMode = SecretMenuMode::StationStations;
                setFrames(FOCUS_SECRET);
            }
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            secretMenuMode = SecretMenuMode::Root;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            return true;
        default:
            return true;
        }
#else
        LOG_WARN("Station browser unavailable");
        secretMenuMode = SecretMenuMode::Root;
        return true;
#endif
    };

    auto handleStationStaNav = [this](uint8_t evt) -> bool {
#if HAS_WIFI && defined(ARCH_ESP32)
        auto &tracker = marauder::StationTracker::instance();
        const auto &aps = tracker.getAccessPoints();
        if (stationApSelection >= aps.size()) {
            secretMenuMode = SecretMenuMode::StationAps;
            return true;
        }
        const auto &ap = aps[stationApSelection];
        const auto &stations = tracker.getStations();
        auto &indices = const_cast<std::vector<size_t> &>(ap.stationIndices);
        size_t total = indices.size();
        switch (evt) {
        case INPUT_BROKER_UP:
            if (total > 0)
                stationStaSelection = (stationStaSelection + total - 1) % total;
            setFastFramerate();
            return true;
        case INPUT_BROKER_DOWN:
            if (total > 0)
                stationStaSelection = (stationStaSelection + 1) % total;
            setFastFramerate();
            return true;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
        case INPUT_BROKER_RIGHT:
            if (stationStaSelection < total) {
                size_t stationIndex = indices[stationStaSelection];
                if (stationIndex < stations.size()) {
                    const auto &sta = stations[stationIndex];
                    tracker.selectStation(stationIndex);
                    wifiPreferredValid = true;
                    wifiPreferredBssid = sta.apBssid;
                    wifiPreferredSsid = ap.ssid;
#if HAS_WIFI && defined(ARCH_ESP32)
                    marauder::WifiAttackController::instance().setPreferredStation(sta.apBssid, sta.mac, sta.channel);
#endif
                    LOG_INFO("Station target set %s -> %s",
                             ap.ssid.empty() ? "<hidden>" : ap.ssid.c_str(),
                             macToString(sta.mac).c_str());
                    secretMenuMode = SecretMenuMode::Root;
                    setFrames(FOCUS_SECRET);
                }
            }
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            secretMenuMode = SecretMenuMode::StationAps;
            setFastFramerate();
            setFrames(FOCUS_SECRET);
            return true;
        default:
            return true;
        }
#else
        LOG_WARN("Station browser unavailable");
        secretMenuMode = SecretMenuMode::Root;
        return true;
#endif
    };

    auto handleDetonateNav = [this](uint8_t evt) -> bool {
        switch (evt) {
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
            sendDetonateCommand();
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_RIGHT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            exitDetonateMode();
            hideSecretMenu();
            setFastFramerate();
            return true;
        default:
            return true;
        }
    };

    auto handleDbMeterNav = [this](uint8_t evt) -> bool {
        switch (evt) {
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_RIGHT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            stopDbMeterMode();
            hideSecretMenu();
            setFastFramerate();
            return true;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
        case INPUT_BROKER_UP:
        case INPUT_BROKER_DOWN:
            setFastFramerate();
            return true;
        default:
            return true;
        }
    };

    auto handleToneGeneratorNav = [this](uint8_t evt) -> bool {
        auto clampFreq = [](int freq) {
            if (freq < 50)
                return 50;
            if (freq > 10000)
                return 10000;
            return freq;
        };
        switch (evt) {
        case INPUT_BROKER_UP:
            toneGeneratorFrequencyHz = clampFreq(static_cast<int>(toneGeneratorFrequencyHz) + 50);
            setFastFramerate();
            return true;
        case INPUT_BROKER_DOWN:
            toneGeneratorFrequencyHz = clampFreq(static_cast<int>(toneGeneratorFrequencyHz) - 50);
            setFastFramerate();
            return true;
        case INPUT_BROKER_RIGHT:
            playToneOnce();
            setFastFramerate();
            return true;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
            toggleTonePlayback();
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            stopToneGeneratorMode();
            hideSecretMenu();
            setFastFramerate();
            return true;
        default:
            return true;
        }
    };

    auto handlePactTimerNav = [this](uint8_t evt) -> bool {
        switch (evt) {
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
            if (pactTimerRecording || pactTimerArming || pactTimerBeepActive) {
                finalizePactString();
            } else if (!pactTimerStartVisible) {
                setFastFramerate();
                return true;
            } else {
                beginPactCountdown();
            }
            setFastFramerate();
            return true;
        case INPUT_BROKER_LEFT:
            addPactShotManual();
            setFastFramerate();
            return true;
        case INPUT_BROKER_UP: {
            if (!(pactTimerRecording || pactTimerArming || pactTimerBeepActive) && !pactShotTimesMs.empty()) {
                if (pactTimerSplitOffset > 0)
                    pactTimerSplitOffset--;
            }
            setFastFramerate();
            return true;
        }
        case INPUT_BROKER_DOWN: {
            if (!(pactTimerRecording || pactTimerArming || pactTimerBeepActive) && !pactShotTimesMs.empty()) {
                size_t shotCount = pactShotTimesMs.size();
                size_t maxOffset = (shotCount > pactSplitsPerPage) ? shotCount - pactSplitsPerPage : 0;
                if (pactTimerSplitOffset < maxOffset)
                    pactTimerSplitOffset++;
            }
            setFastFramerate();
            return true;
        }
        case INPUT_BROKER_RIGHT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            if (pactTimerRecording || pactTimerArming || pactTimerBeepActive) {
                stopPactTimerMode(false);
                secretMenuMode = SecretMenuMode::Root;
                setFrames(FOCUS_SECRET);
            } else if (pactTimerHasResult && evt == INPUT_BROKER_CANCEL) {
                stopPactTimerMode(true);
                pactTimerArming = false;
                pactTimerBeepActive = false;
                pactTimerRecording = false;
                stopPactMic();
                audio::toneOutputStop();
                audio::toneOutputReset();
                pactTimerHasResult = false;
                pactTimerSplitOffset = 0;
                pactTimerTotalMs = 0;
                pactTimerStartVisible = true;
                pactTimerStatus.clear();
                secretMenuMode = SecretMenuMode::PactTimer;
                setFrames(FOCUS_SECRET);
            } else if (pactTimerHasResult) {
                stopPactTimerMode(false);
                setFrames(FOCUS_SECRET);
            } else {
                stopPactTimerMode(true);
                secretMenuMode = SecretMenuMode::Root;
                setFrames(FOCUS_SECRET);
            }
            setFastFramerate();
            return true;
        default:
            return true;
        }
    };

    if (secretMenuMode == SecretMenuMode::WifiAttacks)
        return handleWifiAttackNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::WifiScanner)
        return handleWifiScanNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::StationAps)
        return handleStationApNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::StationStations)
        return handleStationStaNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::Detonate)
        return handleDetonateNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::DbMeter)
        return handleDbMeterNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::ToneGenerator)
        return handleToneGeneratorNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::PactTimer)
        return handlePactTimerNav(inputEvent);
    if (secretMenuMode == SecretMenuMode::MsdCalculator) {
        switch (inputEvent) {
        case INPUT_BROKER_LEFT:
        case INPUT_BROKER_RIGHT:
        case INPUT_BROKER_BACK:
        case INPUT_BROKER_CANCEL:
            stopMsdCalculatorMode();
            hideSecretMenu();
            setFastFramerate();
            return true;
        default:
            return true;
        }
    }

    switch (inputEvent) {
    case INPUT_BROKER_UP:
        secretMenuSelection = (secretMenuSelection + secretMenuItemCount - 1) % secretMenuItemCount;
        setFastFramerate();
        return true;
    case INPUT_BROKER_DOWN:
        secretMenuSelection = (secretMenuSelection + 1) % secretMenuItemCount;
        setFastFramerate();
        return true;
    case INPUT_BROKER_USER_PRESS:
    case INPUT_BROKER_SELECT:
        LOG_INFO("Secret menu selection: %s", secretMenuItemName(selectedSecretMenuEntry(secretMenuSelection)));
        handleSecretMenuSelection();
        setFastFramerate();
        return true;
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_RIGHT:
    case INPUT_BROKER_BACK:
    case INPUT_BROKER_CANCEL:
        hideSecretMenu();
        return true;
    default:
        return true;
    }
}

void Screen::updateSecretGestureProgress(uint8_t inputEvent)
{
    auto isDirectional = [](uint8_t evt) {
        return evt == INPUT_BROKER_UP || evt == INPUT_BROKER_DOWN || evt == INPUT_BROKER_LEFT || evt == INPUT_BROKER_RIGHT;
    };

    if (!isDirectional(inputEvent)) {
        secretGestureProgress = 0;
        secretGestureStartMs = 0;
        return;
    }

    if (secretGestureProgress > 0) {
        uint32_t elapsed = millis() - secretGestureStartMs;
        if (elapsed > 8500) {
            secretGestureProgress = 0;
            secretGestureStartMs = 0;
        }
    }

    if (inputEvent == secretGestureSequence[secretGestureProgress]) {
        secretGestureProgress++;
        if (secretGestureProgress == 1)
            secretGestureStartMs = millis();
        if (secretGestureProgress >= secretGestureLength) {
            secretGestureProgress = 0;
            secretGestureStartMs = 0;
            showSecretToolsMenu();
        }
    } else {
        secretGestureProgress = (inputEvent == secretGestureSequence[0]) ? 1 : 0;
        secretGestureStartMs = secretGestureProgress ? millis() : 0;
    }
}

int Screen::handleInputEvent(const InputEvent *event)
{
    LOG_INPUT("Screen Input event %u! kb %u", event->inputEvent, event->kbchar);
    if (!screenOn)
        return 0;

    // Handle text input notifications specially - pass input to virtual keyboard
    if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
        NotificationRenderer::inEvent = *event;
        static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
        ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
        setFastFramerate(); // Draw ASAP
        ui->update();
        return 0;
    }

#ifdef USE_EINK // the screen is the last input handler, so if an event makes it here, we can assume it will prompt a screen draw.
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
    EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
    handleSetOn(true);                        // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
    setFastFramerate();                       // Draw ASAP
#endif
    if (NotificationRenderer::isOverlayBannerShowing()) {
        NotificationRenderer::inEvent = *event;
        static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
        ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
        setFastFramerate(); // Draw ASAP
        ui->update();

        menuHandler::handleMenuSwitch(dispdev);
        return 0;
    }

    if (handleSecretMenuInput(event->inputEvent))
        return 0;

    if (showingNormalScreen)
        updateSecretGestureProgress(event->inputEvent);

    if (battMeterActive && framesetInfo.positions.battMeter < framesetInfo.frameCount &&
        ui->getUiState()->currentFrame == framesetInfo.positions.battMeter) {
        if (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_BACK ||
            event->inputEvent == INPUT_BROKER_CANCEL) {
            stopBattMeterMode();
            return 0;
        }
    }

    // Use left or right input from a keyboard to move between frames,
    // so long as a mesh module isn't using these events for some other purpose
    if (showingNormalScreen) {

        // Ask any MeshModules if they're handling keyboard input right now
        bool inputIntercepted = false;
        for (MeshModule *module : moduleFrames) {
            if (module && module->interceptingKeyboardInput())
                inputIntercepted = true;
        }

        // If no modules are using the input, move between frames
        if (!inputIntercepted) {
            if (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_ALT_PRESS) {
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_RIGHT || event->inputEvent == INPUT_BROKER_USER_PRESS) {
                showNextFrame();
            } else if (event->inputEvent == INPUT_BROKER_UP_LONG) {
                // Long press up button for fast frame switching
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_DOWN_LONG) {
                // Long press down button for fast frame switching
                showNextFrame();
            } else if (event->inputEvent == INPUT_BROKER_SELECT) {
                if (this->ui->getUiState()->currentFrame == framesetInfo.positions.home) {
                    menuHandler::homeBaseMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.system) {
                    menuHandler::systemBaseMenu();
#if HAS_GPS
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.gps && gps) {
                    menuHandler::positionBaseMenu();
#endif
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.clock) {
                    menuHandler::clockMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.lora) {
                    menuHandler::loraMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.textMessage) {
                    if (devicestate.rx_text_message.from) {
                        menuHandler::messageResponseMenu();
                    } else {
#if defined(M5STACK_UNITC6L)
                        menuHandler::textMessageMenu();
#else
                        menuHandler::textMessageBaseMenu();
#endif
                    }
                } else if (framesetInfo.positions.firstFavorite != 255 &&
                           this->ui->getUiState()->currentFrame >= framesetInfo.positions.firstFavorite &&
                           this->ui->getUiState()->currentFrame <= framesetInfo.positions.lastFavorite) {
                    menuHandler::favoriteBaseMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_lastheard ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_hopsignal ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_distance ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_hopsignal ||
                           this->ui->getUiState()->currentFrame == framesetInfo.positions.nodelist_bearings) {
                    menuHandler::nodeListMenu();
                } else if (this->ui->getUiState()->currentFrame == framesetInfo.positions.wifi) {
                    menuHandler::wifiBaseMenu();
                }
            } else if (event->inputEvent == INPUT_BROKER_BACK) {
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_CANCEL) {
                setOn(false);
            }
        }
    }

    return 0;
}

int Screen::handleAdminMessage(AdminModule_ObserverData *arg)
{
    switch (arg->request->which_payload_variant) {
    // Node removed manually (i.e. via app)
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        setFrames(FOCUS_PRESERVE);
        *arg->result = AdminMessageHandleResult::HANDLED;
        break;

    // Default no-op, in case the admin message observable gets used by other classes in future
    default:
        break;
    }
    return 0;
}

bool Screen::isOverlayBannerShowing()
{
    return NotificationRenderer::isOverlayBannerShowing();
}

} // namespace graphics

#else
graphics::Screen::Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY) {}
#endif // HAS_SCREEN

bool shouldWakeOnReceivedMessage()
{
    /*
    The goal here is to determine when we do NOT wake up the screen on message received:
    - Any ext. notifications are turned on
    - If role is not CLIENT / CLIENT_MUTE / CLIENT_HIDDEN / CLIENT_BASE
    - If the battery level is very low
    */
    if (moduleConfig.external_notification.enabled) {
        return false;
    }
    if (!IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_CLIENT,
                   meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE, meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN,
                   meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)) {
        return false;
    }
    if (powerStatus && powerStatus->getBatteryChargePercent() < 10) {
        return false;
    }
    return true;
}
