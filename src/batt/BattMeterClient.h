#pragma once

#include "configuration.h"
#include "concurrency/OSThread.h"
#include <Arduino.h>

#if HAS_WIFI
#include <WiFi.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#endif

#include "batt/mesh_config.h"

#if HAS_WIFI

class BattMeterClient : public concurrency::OSThread
{
  public:
    BattMeterClient();

    void start();
    void start(bool longRange);
    void stop();
    bool isActive() const { return active; }

    bool hasReading() const { return lastPercent >= 0; }
    int getLastPercent() const { return lastPercent; }
    float getLastVoltage() const { return lastVoltage; }
    uint32_t getLastUpdateMs() const { return lastUpdateMs; }

    bool isMeshReady() const;
    bool hasMeshPeers() const;
    bool sendMeshCommand(const String &commandKey, const String &commandValue);
    bool isLongRangeMode() const { return longRangeMode; }

  protected:
    int32_t runOnce() override;

  private:
    void handleMessage(uint32_t from, String &msg);
    friend void meshReceiveTrampoline(uint32_t from, String &msg);

    bool active = false;
    WiFiMode_t previousWifiMode = WIFI_OFF;
    bool meshInitialized = false;
    bool longRangeMode = false;

    volatile int lastPercent = -1;
    volatile float lastVoltage = 0.0f;
    volatile uint32_t lastUpdateMs = 0;
};

#else

class BattMeterClient
{
  public:
    BattMeterClient() = default;
    void start() {}
    void start(bool) {}
    void stop() {}
    bool isActive() const { return false; }
    bool hasReading() const { return false; }
    int getLastPercent() const { return -1; }
    float getLastVoltage() const { return 0.0f; }
    uint32_t getLastUpdateMs() const { return 0; }

    bool isMeshReady() const { return false; }
    bool hasMeshPeers() const { return false; }
    bool sendMeshCommand(const String &, const String &) { return false; }
    bool isLongRangeMode() const { return false; }
};

#endif

extern BattMeterClient *battMeterClient;
