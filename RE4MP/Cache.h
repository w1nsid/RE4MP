#pragma once
#include "Config.h"
#include "Network.h"

DWORD base_addr;

// partner data
int* playerTwoPtr = nullptr;
bool playerTwoReady = false;
float* playerTwoPos = new float[3] {5000.0f, 50.0f, -3914.870605f};

// networking
RE4MPConfig g_config;
RE4MPNetwork g_net;
HMODULE g_hModule = NULL;