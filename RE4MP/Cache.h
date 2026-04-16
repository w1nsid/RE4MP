#pragma once
#include "Config.h"
#include "Network.h"
#include "EntitySync.h"

DWORD base_addr;

// partner data
int* playerTwoPtr = nullptr;
bool playerTwoReady = false;
float* playerTwoPos = new float[3] {5000.0f, 50.0f, -3914.870605f};
float remoteAshleyPos[3] = {0.0f, 0.0f, 0.0f};

// entity sync
bool g_isHost = true;  // first player to connect is host (authoritative for enemies)

// networking
RE4MPConfig g_config;
RE4MPNetwork g_net;
HMODULE g_hModule = NULL;