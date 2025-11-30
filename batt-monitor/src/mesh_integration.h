#ifndef MESH_INTEGRATION_H
#define MESH_INTEGRATION_H

#include <painlessMesh.h>
#include "mesh_config.h"

// We declare these here so main.cpp can use them (extern).
extern painlessMesh mesh;
extern bool isPairing;    // IMPORTANT: "extern bool isPairing;"

// Functions that main.cpp will call
void meshSetup(bool longRange);
void meshLoop();

// Allow callers to select credentials before meshSetup.
void meshConfigure(const char *prefix, const char *password, uint16_t port);

#endif
