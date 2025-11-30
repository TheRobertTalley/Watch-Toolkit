#include "batt/BattMeterClient.h"

#if HAS_WIFI

#include <algorithm>
#include <ArduinoJson.h>
#include "mesh_integration.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_wifi.h>
#endif

BattMeterClient *battMeterClient = nullptr;

#if defined(ARDUINO_ARCH_ESP32)
static void configureWifiProtocol(bool longRange)
{
    if (longRange) {
        // Match Carmi-Claymore: enable LR on both STA and AP interfaces so mesh nodes
        // using either role can communicate reliably.
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_LR);
        LOG_INFO("BattMeterClient configured to long-range WiFi protocol (STA+AP)");
    } else {
        // Restore standard 11b/g/n on both STA and AP.
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        LOG_INFO("BattMeterClient configured to standard WiFi protocol (STA+AP)");
    }
}
#else
static void configureWifiProtocol(bool) {}
#endif

namespace
{
constexpr uint32_t kMeshUpdateDelayMs = 10;
constexpr uint32_t kConnectionWatchdogMs = 30000;
constexpr float kBatteryEmptyVolts = 9.0f;
constexpr float kBatteryFullVolts = 12.6f;

StaticJsonDocument<128> receiveFilterDoc;
bool receiveFilterInitialized = false;

void ensureReceiveFilter()
{
    if (receiveFilterInitialized)
        return;

    receiveFilterDoc.clear();
    receiveFilterDoc["remote"]["gunbatt"]["voltage"] = true;
    receiveFilterDoc["remote"]["gunbatt"]["percentage"] = true;
    receiveFilterDoc["device"]["mybatt"]["voltage"] = true;
    receiveFilterDoc["device"]["mybatt"]["percentage"] = true;
    receiveFilterInitialized = true;
}

int voltageToPercent(float voltage)
{
    float percent = (voltage - kBatteryEmptyVolts) / (kBatteryFullVolts - kBatteryEmptyVolts) * 100.0f;
    percent = constrain(percent, 0.0f, 100.0f);
    return static_cast<int>(percent + 0.5f);
}
} // namespace

static BattMeterClient *gInstance = nullptr;

void meshReceiveTrampoline(uint32_t from, String &msg)
{
    if (gInstance) {
        gInstance->handleMessage(from, msg);
    }
}

BattMeterClient::BattMeterClient() : concurrency::OSThread("BattMeterClient")
{
    setInterval(UINT32_MAX);
}

void BattMeterClient::start()
{
    start(false);
}

void BattMeterClient::start(bool longRange)
{
    if (active && longRangeMode == longRange)
        return;

    if (active)
        stop();

    longRangeMode = longRange;
    // Hard reset WiFi before reconfiguring for a tool-specific mesh.
    previousWifiMode = WiFi.getMode();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(50); // allow radio to settle before re-enabling
    WiFi.mode(WIFI_STA);

#if defined(ARDUINO_ARCH_ESP32)
    configureWifiProtocol(longRange);
#endif

    // Select mesh credentials based on the requested mode.
    const char *meshPrefix = longRange ? DETONATE_MESH_PREFIX : BATT_MESH_PREFIX;
    const char *meshPassword = longRange ? DETONATE_MESH_PASSWORD : BATT_MESH_PASSWORD;
    uint16_t meshPort = longRange ? DETONATE_MESH_PORT : BATT_MESH_PORT;
    meshConfigure(meshPrefix, meshPassword, meshPort);

    if (meshInitialized) {
        mesh.stop();
        meshInitialized = false;
    }
    lastPercent = -1;
    lastVoltage = 0.0f;
    meshSetup(longRange);
    meshInitialized = true;
    gInstance = this;
    mesh.onReceive(meshReceiveTrampoline);
    active = true;
    setInterval(kMeshUpdateDelayMs);
    LOG_INFO("BattMeterClient started (LR=%s)", longRange ? "yes" : "no");
}

void BattMeterClient::stop()
{
    if (!active)
        return;
    active = false;
    if (meshInitialized) {
        mesh.stop();
        meshInitialized = false;
    }
    gInstance = nullptr;
    setInterval(UINT32_MAX);
    if (previousWifiMode == WIFI_OFF)
        WiFi.mode(WIFI_OFF);
    else
        WiFi.mode(previousWifiMode);
    LOG_INFO("BattMeterClient stopped");
}

int32_t BattMeterClient::runOnce()
{
    if (!active)
        return UINT32_MAX;
    meshLoop();
    static uint32_t lastStaleLogMs = 0;
    uint32_t now = millis();
    if (lastPercent >= 0 && (now - lastUpdateMs) > kConnectionWatchdogMs && (now - lastStaleLogMs) > kConnectionWatchdogMs) {
        LOG_WARN("Batt meter telemetry stale; keeping last known values");
        lastStaleLogMs = now;
    }
    return kMeshUpdateDelayMs;
}

void BattMeterClient::handleMessage(uint32_t, String &msg)
{
    // In detonate (long-range) mode we should not treat incoming JSON as
    // battery telemetry for the UI. Detonate uses the mesh only for commands.
    if (longRangeMode) {
        LOG_DEBUG("BattMeterClient ignoring battery JSON while in detonate mode");
        return;
    }

    ensureReceiveFilter();

    StaticJsonDocument<256> doc;
    auto err = deserializeJson(doc, msg, DeserializationOption::Filter(receiveFilterDoc));
    if (err) {
        LOG_WARN("Batt meter JSON parse failed (%s), len=%u", err.c_str(), msg.length());
        return;
    }

    bool updated = false;

    auto tryUpdate = [&](JsonVariant value) -> bool {
        if (value.isNull())
            return false;

        float voltage = value["voltage"] | 0.0f;
        int percent = value["percentage"] | -1;

        if (percent < 0 && voltage > 0.0f) {
            percent = voltageToPercent(voltage);
        }

        if (percent < 0)
            return false;

        if (voltage <= 0.0f && percent <= 0)
            return false;

        lastVoltage = voltage;
        lastPercent = std::min(100, std::max(0, percent));
        lastUpdateMs = millis();
        LOG_INFO("Batt meter update: %.2fV %d%%", lastVoltage, lastPercent);
        updated = true;
        return true;
    };

    if (!tryUpdate(doc["remote"]["gunbatt"])) {
        tryUpdate(doc["device"]["mybatt"]);
    }

    if (!updated)
        LOG_DEBUG("Batt meter JSON missing battery fields");
}

bool BattMeterClient::isMeshReady() const
{
    return active && meshInitialized;
}

bool BattMeterClient::hasMeshPeers() const
{
    if (!active || !meshInitialized)
        return false;

    if (!mesh.getNodeList().empty())
        return true;

    return lastPercent >= 0;
}

bool BattMeterClient::sendMeshCommand(const String &commandKey, const String &commandValue)
{
    if (!active || !meshInitialized)
        return false;

    StaticJsonDocument<256> doc;
    doc["meshCommands"][commandKey] = commandValue;

    String json;
    serializeJson(doc, json);
    mesh.sendBroadcast(json);
    LOG_INFO("BattMeterClient sent mesh command %s=%s", commandKey.c_str(), commandValue.c_str());
    return true;
}

#else

BattMeterClient *battMeterClient = nullptr;

#endif
