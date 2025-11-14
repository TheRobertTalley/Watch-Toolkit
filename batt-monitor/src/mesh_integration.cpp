#include "mesh_integration.h" // Must include our new header
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include "mesh_config.h"


// --------------------------------------------------------------------
// Actual definitions of the extern variables:
painlessMesh mesh;          // "extern painlessMesh mesh;" from .h
bool isPairing = true;      // CHANGED! removed "static" so main.cpp can see it
// --------------------------------------------------------------------

static const unsigned long PAIRING_DURATION = 15000; // 15 seconds for pairing
static unsigned long pairingStartTime       = 0;

// Broadcast pairing data every 500 ms
static const unsigned long BROADCAST_INTERVAL = 500;
static unsigned long lastBroadcast            = 0;

// Track received messages for pairing
std::map<uint64_t, int> receivedMessages; // Device ID -> message count
std::vector<uint64_t> pairedDevices;      // List of paired devices

// --------------------------------------------------------------------
// Send out a simple pairing broadcast
// --------------------------------------------------------------------
void broadcastPairingData() {
    delay(random(100, 500)); // Random delay to reduce collisions

    DynamicJsonDocument doc(200);
    doc["device"]    = ESP.getEfuseMac(); // Unique ID
    doc["message"]   = "Pairing broadcast";
    doc["timestamp"] = millis();

    String jsonString;
    serializeJson(doc, jsonString);

    mesh.sendBroadcast(jsonString);
    Serial.println("Pairing broadcast sent: " + jsonString);
}

// --------------------------------------------------------------------
// Mesh Setup (called from main.cpp setup())
// --------------------------------------------------------------------
void meshSetup() {
    Serial.println("Initializing mesh network...");

    mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
    // Initialize the mesh
    mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

    // We'll not set .onReceive(...) here, because main.cpp does it with `receivedCallback(...)`

    // onNewConnection
    mesh.onNewConnection([](uint32_t nodeId) {
        Serial.printf("New connection, nodeId = %u\n", nodeId);
    });

    // onChangedConnections
    mesh.onChangedConnections([]() {
        Serial.println("Changed connections");
    });

    // Start pairing mode
    pairingStartTime = millis();
    isPairing        = true;
}

// --------------------------------------------------------------------
// Mesh Loop (called from main.cpp loop())
// --------------------------------------------------------------------
void meshLoop() {
    mesh.update();

    // During pairing, broadcast pairing data every 500 ms
    if (isPairing) {
        unsigned long now = millis();
        if (now - lastBroadcast >= BROADCAST_INTERVAL) {
            lastBroadcast = now;
            broadcastPairingData();
        }

        // If pairing time expired, finalize
        if (millis() - pairingStartTime > PAIRING_DURATION) {
            isPairing = false;
            Serial.println("Pairing mode ended. Processing paired devices...");

            // Determine threshold for "paired"
            int pairingThreshold = (PAIRING_DURATION / BROADCAST_INTERVAL) * 0.6f; 
            for (auto &entry : receivedMessages) {
                uint64_t device    = entry.first;
                int messageCount   = entry.second;
                if (messageCount >= pairingThreshold) {
                    pairedDevices.push_back(device);
                    Serial.printf("Paired with device: %llu (Messages: %d)\n", device, messageCount);
                }
            }

            // List all paired
            Serial.println("Paired devices:");
            for (uint64_t device : pairedDevices) {
                Serial.printf(" - Device: %llu\n", device);
            }
        }
    }
}

// No onReceive duplication here; main.cpp handles receivedCallback(...)
