#ifndef JSON_HANDLER_H
#define JSON_HANDLER_H

#include <ArduinoJson.h>
#include <painlessMesh.h>
#include <WiFi.h>

/**
 * We keep a single global JSON document in memory, `jsonDoc`,
 * and populate it with real data whenever we have it.
 */

static const size_t JSON_BUFFER_SIZE = 2048;
DynamicJsonDocument jsonDoc(JSON_BUFFER_SIZE);

// =================================================================================
// Initialize JSON with Default Values (mostly zeroes or null)
// =================================================================================
void initializeJson() {
    // device info
    jsonDoc["device"]["deviceID"]         = WiFi.macAddress(); // Actual MAC as ID
    jsonDoc["device"]["deviceName"]       = "Unknown";
    jsonDoc["device"]["batteryLevel"]     = 0;     // Default to 0
    jsonDoc["device"]["firmwareVersion"]  = "v0.0.0";
    jsonDoc["device"]["deviceStatus"]     = "active";

    jsonDoc["device"]["mybatt"]["voltage"]    = 0.0;
    jsonDoc["device"]["mybatt"]["percentage"] = 0;

    // remote info
    jsonDoc["remote"]["id"]                     = "00:00:00:00:00:00";
    jsonDoc["remote"]["gunbatt"]["voltage"]     = 0.0;
    jsonDoc["remote"]["gunbatt"]["percentage"]  = 0;

    // combat metrics
    jsonDoc["combatMetrics"]["shotCounter"]       = 0;
    jsonDoc["combatMetrics"]["hitCounter"]        = 0;
    jsonDoc["combatMetrics"]["missCounter"]       = 0;
    jsonDoc["combatMetrics"]["ammoCount"]         = 0;
    jsonDoc["combatMetrics"]["reloadStatus"]      = false;
    jsonDoc["combatMetrics"]["targetAcquired"]    = false;
    jsonDoc["combatMetrics"]["targetHit"]         = false;
    jsonDoc["combatMetrics"]["distanceToTarget"]  = 0;

    // game & timer data
    jsonDoc["gameData"]["gameMode"]    = "none";
    jsonDoc["gameData"]["gameTimer"]   = 0;
    jsonDoc["gameData"]["roundNumber"] = 1;
    jsonDoc["gameData"]["teamID"]      = "none";
    jsonDoc["gameData"]["playerRole"]  = "none";

    // alerts & notifications
    jsonDoc["alerts"]["hitDetected"]       = false;
    jsonDoc["alerts"]["lowBatteryAlert"]   = false;
    jsonDoc["alerts"]["ammoEmptyAlert"]    = false;
    jsonDoc["alerts"]["jamDetected"]       = false;
    jsonDoc["alerts"]["overheatAlert"]     = false;

    // environment data
    jsonDoc["environment"]["temperature"]   = 0.0;
    jsonDoc["environment"]["humidity"]      = 0;
    jsonDoc["environment"]["windSpeed"]     = 0;
    jsonDoc["environment"]["windDirection"] = "N";
    jsonDoc["environment"]["ambientLight"]  = 0;

    // position data
    jsonDoc["position"]["latitude"]  = 0.0;
    jsonDoc["position"]["longitude"] = 0.0;
    jsonDoc["position"]["altitude"]  = 0.0;
    jsonDoc["position"]["heading"]   = 0.0;
    jsonDoc["position"]["speed"]     = 0.0;

    // custom
    jsonDoc["customCommands"]["command"]        = "none";
    jsonDoc["customCommands"]["acknowledgment"] = false;
    jsonDoc["customCommands"]["errorCode"]      = 0;
    jsonDoc["customCommands"]["customMessage"]  = "none";

    // debug & diagnostics
    jsonDoc["debug"]["signalStrength"] = 0;
    jsonDoc["debug"]["packetLoss"]     = 0;
    jsonDoc["debug"]["uptime"]         = 0;

    // mesh commands
    jsonDoc["meshCommands"]["DETONATE"] = "NONE";
    jsonDoc["meshCommands"]["LAUNCH"]   = "NONE";
}

// =================================================================================
// Setters for each section
// =================================================================================
void setDeviceInfo(const String& name, int battLevel, const String& firmware, const String& status) {
    jsonDoc["device"]["deviceName"]       = name;
    jsonDoc["device"]["batteryLevel"]     = battLevel;
    jsonDoc["device"]["firmwareVersion"]  = firmware;
    jsonDoc["device"]["deviceStatus"]     = status;
}

void updateDeviceBattery(float voltage, int percentage) {
    jsonDoc["device"]["mybatt"]["voltage"]    = voltage;
    jsonDoc["device"]["mybatt"]["percentage"] = percentage;
}

void updateRemoteBattery(const String& mac, float voltage, int percentage) {
    jsonDoc["remote"]["id"]                      = mac;
    jsonDoc["remote"]["gunbatt"]["voltage"]      = voltage;
    jsonDoc["remote"]["gunbatt"]["percentage"]   = percentage;
}

void setCombatMetrics(int shotCount, int hitCount, int missCount, int ammo,
                      bool reload, bool tgtAcq, bool tgtHit, int distTgt) {
    jsonDoc["combatMetrics"]["shotCounter"]      = shotCount;
    jsonDoc["combatMetrics"]["hitCounter"]       = hitCount;
    jsonDoc["combatMetrics"]["missCounter"]      = missCount;
    jsonDoc["combatMetrics"]["ammoCount"]        = ammo;
    jsonDoc["combatMetrics"]["reloadStatus"]     = reload;
    jsonDoc["combatMetrics"]["targetAcquired"]   = tgtAcq;
    jsonDoc["combatMetrics"]["targetHit"]        = tgtHit;
    jsonDoc["combatMetrics"]["distanceToTarget"] = distTgt;
}

void setGameData(const String& mode, int timer, int round, const String& team, const String& role) {
    jsonDoc["gameData"]["gameMode"]    = mode;
    jsonDoc["gameData"]["gameTimer"]   = timer;
    jsonDoc["gameData"]["roundNumber"] = round;
    jsonDoc["gameData"]["teamID"]      = team;
    jsonDoc["gameData"]["playerRole"]  = role;
}

void setAlerts(bool hitDetected, bool lowBatt, bool ammoEmpty, bool jam, bool overheat) {
    jsonDoc["alerts"]["hitDetected"]     = hitDetected;
    jsonDoc["alerts"]["lowBatteryAlert"] = lowBatt;
    jsonDoc["alerts"]["ammoEmptyAlert"]  = ammoEmpty;
    jsonDoc["alerts"]["jamDetected"]     = jam;
    jsonDoc["alerts"]["overheatAlert"]   = overheat;
}

void setEnvironment(float temp, int humidity, int windSpd, const String& windDir, int light) {
    jsonDoc["environment"]["temperature"]   = temp;
    jsonDoc["environment"]["humidity"]      = humidity;
    jsonDoc["environment"]["windSpeed"]     = windSpd;
    jsonDoc["environment"]["windDirection"] = windDir;
    jsonDoc["environment"]["ambientLight"]  = light;
}

void setPosition(float lat, float lon, float alt, float heading, float speed) {
    jsonDoc["position"]["latitude"]  = lat;
    jsonDoc["position"]["longitude"] = lon;
    jsonDoc["position"]["altitude"]  = alt;
    jsonDoc["position"]["heading"]   = heading;
    jsonDoc["position"]["speed"]     = speed;
}

void setCustomCommand(const String& cmd, bool ack, int err, const String& message) {
    jsonDoc["customCommands"]["command"]        = cmd;
    jsonDoc["customCommands"]["acknowledgment"] = ack;
    jsonDoc["customCommands"]["errorCode"]      = err;
    jsonDoc["customCommands"]["customMessage"]  = message;
}

void setDebugData(int signal, int pLoss, long upTime) {
    jsonDoc["debug"]["signalStrength"] = signal;
    jsonDoc["debug"]["packetLoss"]     = pLoss;
    jsonDoc["debug"]["uptime"]         = upTime;
}

// Mesh Commands
void setCommandDetonate(const String& command) {
    jsonDoc["meshCommands"]["DETONATE"] = command;
}

void setCommandLaunch(const String& command) {
    jsonDoc["meshCommands"]["LAUNCH"] = command;
}

// =================================================================================
// Broadcast JSON
// =================================================================================
void broadcastJson(painlessMesh& mesh) {
    String jsonString;
    serializeJson(jsonDoc, jsonString);
    mesh.sendBroadcast(jsonString);
    Serial.println("Broadcasting JSON: " + jsonString);
}

// =================================================================================
// Parse Received JSON
// =================================================================================
void parseReceivedJson(const String& jsonString) {
    DynamicJsonDocument incomingDoc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(incomingDoc, jsonString);
    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        return;
    }

    Serial.println("? JSON Received and Parsed:");

    // Example device ID
    const char* devID = incomingDoc["device"]["deviceID"] | "none";
    Serial.printf("Device ID: %s\n", devID);

    // Example DETONATE
    const char* meshDet = incomingDoc["meshCommands"]["DETONATE"] | "NONE";
    Serial.printf("DETONATE: %s\n", meshDet);

    // Example position
    float lat = incomingDoc["position"]["latitude"]  | 0.0;
    float lon = incomingDoc["position"]["longitude"] | 0.0;
    Serial.printf("Lat: %.6f, Long: %.6f\n", lat, lon);

    // If the incoming JSON has a "remote->gunbatt" section, update.
    if (incomingDoc.containsKey("remote")) {
        if (incomingDoc["remote"].containsKey("gunbatt")) {
            float gunVolt = incomingDoc["remote"]["gunbatt"]["voltage"]    | 0.0;
            int   gunPct  = incomingDoc["remote"]["gunbatt"]["percentage"] | 0;
            // Only update if there's something actually sent
            if (gunVolt != 0.0 || gunPct != 0) {
                // We assume remote ID is in "remote->id"
                const char* remoteID = incomingDoc["remote"]["id"] | "00:00:00:00:00:00";
                updateRemoteBattery(remoteID, gunVolt, gunPct);
                Serial.printf("Updated gunbatt from remote: ID=%s, volt=%.2f, pct=%d\n",
                              remoteID, gunVolt, gunPct);
            }
        }
    }

    // Merge or do other logic if needed
}

// =================================================================================
// Simple Accessors
// =================================================================================
String getDeviceId() {
    return jsonDoc["device"]["deviceID"].as<String>();
}

float getBatteryVoltage() {
    return jsonDoc["device"]["mybatt"]["voltage"].as<float>();
}

int getBatteryPercentage() {
    return jsonDoc["device"]["mybatt"]["percentage"].as<int>();
}

#endif
