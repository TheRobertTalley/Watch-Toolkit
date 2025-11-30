#pragma once

// Credentials used by the BAtt-Meter mesh network.
// Sourced from https://github.com/TheRobertTalley/BAtt-Meter
#define BATT_MESH_PREFIX "StingrayNetwork2"
#define BATT_MESH_PASSWORD "meshyPassword2"
#define BATT_MESH_PORT 5555

// Credentials used by the detonate (claymore) mesh network.
// We keep these identical to the BAtt-Meter mesh so existing devices
// do not need firmware changes; separation is handled by WiFi LR/standard
// mode and by restarting WiFi + mesh per tool selection.
#define DETONATE_MESH_PREFIX BATT_MESH_PREFIX
#define DETONATE_MESH_PASSWORD BATT_MESH_PASSWORD
#define DETONATE_MESH_PORT BATT_MESH_PORT
