#pragma once
#include <Windows.h>

struct RE4MPConfig {
    char serverIP[64];
    int serverPort;
    bool isHost;  // true = authoritative for enemy/boss state
};

inline void LoadConfig(RE4MPConfig* config, HMODULE hModule)
{
    // Find re4mp.ini next to the DLL
    char iniPath[MAX_PATH];
    GetModuleFileNameA(hModule, iniPath, MAX_PATH);
    // Strip DLL filename to get directory
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(iniPath, MAX_PATH, "re4mp.ini");

    // Defaults
    strcpy_s(config->serverIP, "127.0.0.1");
    config->serverPort = 27015;
    config->isHost = true;

    // Read ServerIP
    GetPrivateProfileStringA("RE4MP", "ServerIP", "127.0.0.1", config->serverIP, sizeof(config->serverIP), iniPath);

    // Read ServerPort
    config->serverPort = GetPrivateProfileIntA("RE4MP", "ServerPort", 27015, iniPath);

    // Read HostMode (1=host, 0=client)
    config->isHost = (GetPrivateProfileIntA("RE4MP", "HostMode", 1, iniPath) != 0);
}
