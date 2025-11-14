#include "batt/BattMeterClient.h"

#if HAS_WIFI

#include <algorithm>
#include <ArduinoJson.h>
#include "mesh_integration.h"

BattMeterClient *battMeterClient = nullptr;

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
    if (active)
        return;
    previousWifiMode = WiFi.getMode();
    if (previousWifiMode != WIFI_STA) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.disconnect(true, true);
    if (meshInitialized) {
        mesh.stop();
        meshInitialized = false;
    }
    lastPercent = -1;
    lastVoltage = 0.0f;
    meshSetup();
    meshInitialized = true;
    gInstance = this;
    mesh.onReceive(meshReceiveTrampoline);
    active = true;
    setInterval(kMeshUpdateDelayMs);
    LOG_INFO("BattMeterClient started");
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
    if (lastPercent >= 0 && (millis() - lastUpdateMs) > kConnectionWatchdogMs) {
        lastPercent = -1;
    }
    return kMeshUpdateDelayMs;
}

void BattMeterClient::handleMessage(uint32_t, String &msg)
{
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

        // Skip placeholder readings that report both 0 V and 0%.
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

#else

BattMeterClient *battMeterClient = nullptr;

#endif
