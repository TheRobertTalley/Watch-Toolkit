/******************************************************
 * main.cpp (T-Watch S3 Mesh Battery Serial Monitor)
 * Minimal build: mesh + Serial log only
 ******************************************************/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <painlessMesh.h>

#include "mesh_integration.h"
#include "mesh_config.h"

namespace {
constexpr float kBatteryEmptyVolts = 9.0f;   // 3S pack near empty
constexpr float kBatteryFullVolts  = 12.6f;  // 3S pack fully charged
constexpr unsigned long kMessageTimeoutMs = 13000UL; // 13 seconds

float batteryVoltage = 0.0f;
float batteryPercentage = 0.0f;
bool batteryConnected = false;
unsigned long lastMessageTime = 0;

StaticJsonDocument<256> receiveFilter;
bool filterInitialized = false;

void ensureReceiveFilter() {
    if (filterInitialized) {
        return;
    }
    receiveFilter.clear();
    receiveFilter["remote"]["id"] = true;
    receiveFilter["remote"]["gunbatt"]["voltage"] = true;
    receiveFilter["remote"]["gunbatt"]["percentage"] = true;
    filterInitialized = true;
}

float voltageToPercentage(float voltage) {
    float percent = (voltage - kBatteryEmptyVolts) /
                    (kBatteryFullVolts - kBatteryEmptyVolts) * 100.0f;
    return constrain(percent, 0.0f, 100.0f);
}

void logBatteryUpdate(uint32_t from, const char *remoteId) {
    Serial.printf("[Mesh %u | %s] Gun voltage: %.2f V (%.0f%%)\n",
                  from,
                  remoteId ? remoteId : "unknown",
                  batteryVoltage,
                  batteryPercentage);
}

void checkForTimeout() {
    if (batteryConnected && (millis() - lastMessageTime) > kMessageTimeoutMs) {
        batteryConnected = false;
        Serial.println("Battery data timeout (>13s). Waiting for next packet...");
    }
}
} // namespace

void receivedCallback(uint32_t from, String &msg);

void setup() {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
        delay(10);
    }

    Serial.println();
    Serial.println("T-Watch S3 Mesh Battery Monitor (Serial Only)");

    ensureReceiveFilter();
    meshSetup();
    mesh.onReceive(receivedCallback);
}

void loop() {
    meshLoop();
    checkForTimeout();
}

void receivedCallback(uint32_t from, String &msg) {
    StaticJsonDocument<512> doc;
    DeserializationError error =
        deserializeJson(doc, msg, DeserializationOption::Filter(receiveFilter));
    if (error) {
        Serial.printf("JSON parse failed from %u: %s\n", from, error.c_str());
        return;
    }

    JsonVariant remote = doc["remote"];
    if (remote.isNull()) {
        return;
    }

    JsonVariant gunBattery = remote["gunbatt"];
    if (!gunBattery.isNull() && gunBattery.containsKey("voltage")) {
        batteryVoltage = gunBattery["voltage"].as<float>();
        batteryPercentage = voltageToPercentage(batteryVoltage);
        batteryConnected = true;
        lastMessageTime = millis();

        const char *remoteId = remote["id"] | "unknown";
        logBatteryUpdate(from, remoteId);
    }
}
