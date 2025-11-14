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
    void stop();
    bool isActive() const { return active; }

    bool hasReading() const { return lastPercent >= 0; }
    int getLastPercent() const { return lastPercent; }
    float getLastVoltage() const { return lastVoltage; }
    uint32_t getLastUpdateMs() const { return lastUpdateMs; }

  protected:
    int32_t runOnce() override;

  private:
    void handleMessage(uint32_t from, String &msg);
    friend void meshReceiveTrampoline(uint32_t from, String &msg);

    bool active = false;
    WiFiMode_t previousWifiMode = WIFI_OFF;
    bool meshInitialized = false;

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
    void stop() {}
    bool isActive() const { return false; }
    bool hasReading() const { return false; }
    int getLastPercent() const { return -1; }
    float getLastVoltage() const { return 0.0f; }
    uint32_t getLastUpdateMs() const { return 0; }
};

#endif

extern BattMeterClient *battMeterClient;
