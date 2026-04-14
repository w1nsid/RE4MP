// RE4MP.cpp : Multiplayer mod for Resident Evil 4 (2005 PC)
// Sends local player position over UDP, receives remote player position for partner AI
//

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "detours.h"
#include <iostream>
#include <stdio.h>
#include <share.h>
#include "RE4MP.h"
#include "hooks.h"
#include "Re4Detours.h"

#include "../version.h"

static FILE* g_logFile = NULL;

void DbgLog(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    printf("%s", buf);
    if (g_logFile) {
        fputs(buf, g_logFile);
        fflush(g_logFile);
    }
}

DWORD WINAPI MainThread(LPVOID param) {

    // Open log file next to the DLL (shared read so we can tail it)
    char logPath[MAX_PATH];
    GetModuleFileNameA((HMODULE)param, logPath, MAX_PATH);
    char* lastSlash = strrchr(logPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(logPath, MAX_PATH, "re4mp.log");
    g_logFile = _fsopen(logPath, "w", _SH_DENYWR);

    // Allocate a debug console
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);

    DbgLog("========================================\n");
    DbgLog("  RE4MP Client v%s\n", RE4MP_VERSION);
    DbgLog("  Built: %s\n", RE4MP_BUILD_DATE);
    DbgLog("========================================\n");
    DbgLog("[RE4MP] DLL loaded, MainThread started\n");

    base_addr = (DWORD)GetModuleHandleA(0);
    DbgLog("[RE4MP] Base address: 0x%08X\n", base_addr);

    DbgLog("[RE4MP] Hooking game functions...\n");
    HookFunctions(base_addr);
    DbgLog("[RE4MP] HookFunctions done\n");

    DbgLog("[RE4MP] Installing detours...");
    DetourFunctions(base_addr);
    DbgLog(" addresses saved (hooks deferred until spawn)\n");

    DbgLog("[RE4MP] Applying code injection...\n");
    CodeInjection(base_addr);
    DbgLog("[RE4MP] CodeInjection done\n");

    // Load config from re4mp.ini next to the DLL
    LoadConfig(&g_config, g_hModule);
    DbgLog("[RE4MP] Config loaded: ServerIP=%s, ServerPort=%d\n", g_config.serverIP, g_config.serverPort);

    // Initialize networking
    if (!InitNetwork(&g_net, &g_config)) {
        DbgLog("[RE4MP] ERROR: Failed to initialize network\n");
    }
    else {
        DbgLog("[RE4MP] Network initialized, connected to %s:%d\n", g_config.serverIP, g_config.serverPort);
    }

    DbgLog("[RE4MP] Entering main loop\n");
    bool f5Held = false;
    DWORD lastAutoSpawn = 0;

    while (true) {

        // --- Leon clone validity check (room transition / death) ---
        if (playerTwoReady && playerTwoPtr) {
            __try {
                if (!IsCEmValid(playerTwoPtr)) {
                    DbgLog("[RE4MP] Leon clone lost (room transition or death)\n");
                    playerTwoReady = false;
                    playerTwoPtr = nullptr;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                playerTwoReady = false;
                playerTwoPtr = nullptr;
            }
        }

        // --- Auto-respawn Leon clone after room transition ---
        if (g_net.initialized && !playerTwoReady && playerTwoWasActive && EmSetEvent) {
            DWORD now = GetTickCount();
            if (now - lastAutoSpawn > 2000) {
                lastAutoSpawn = now;
                float* myPos = GetPlayerPosition();
                if (myPos) {
                    __try {
                        int* clone = SpawnLeonClone(myPos);
                        if (clone) {
                            playerTwoPtr = clone;
                            if (!g_detoursAttached) AttachDetours();
                            playerTwoReady = true;
                            DbgLog("[RE4MP] Leon clone auto-respawned\n");
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        DbgLog("[RE4MP] Auto-respawn crashed\n");
                    }
                }
            }
        }

        // F5: Spawn Leon clone (first time or manual respawn)
        if (GetAsyncKeyState(VK_F5) & 0x8000) {
            if (!f5Held) {
                f5Held = true;
                if (playerTwoReady) {
                    DbgLog("[RE4MP] F5 pressed — Leon clone already active\n");
                } else {
                    DbgLog("[RE4MP] F5 pressed — spawning Leon clone\n");
                    float* myPos = GetPlayerPosition();
                    if (myPos) {
                        __try {
                            int* clone = SpawnLeonClone(myPos);
                            if (clone) {
                                playerTwoPtr = clone;
                                if (!g_detoursAttached) AttachDetours();
                                playerTwoReady = true;
                                playerTwoWasActive = true;
                                DbgLog("[RE4MP] Leon clone ready!\n");
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            DbgLog("[RE4MP] CRASH spawning Leon clone! code=0x%08X\n", GetExceptionCode());
                        }
                    } else {
                        DbgLog("[RE4MP] Cannot spawn — player pointer not ready\n");
                    }
                }
            }
        } else {
            f5Held = false;
        }

        // --- Network send: broadcast our Leon's position + rotation ---
        if (g_net.initialized && playerTwoReady) {
            int* playerPtr = PlayerPointer();
            if (playerPtr) {
                float* myPos = GetPlayerPosition();
                float* myAng = GetCEmAng(playerPtr);
                if (myPos && myAng) {
                    RE4MPPacket sendPkt;
                    memset(&sendPkt, 0, sizeof(sendPkt));
                    sendPkt.type = PKT_POSITION;
                    sendPkt.pos[0] = myPos[0];
                    sendPkt.pos[1] = myPos[1];
                    sendPkt.pos[2] = myPos[2];
                    sendPkt.ang[0] = myAng[0];
                    sendPkt.ang[1] = myAng[1];
                    sendPkt.ang[2] = myAng[2];
                    SendPacket(&g_net, &sendPkt);
                }
            }
        }

        // --- Network recv: apply remote player's position to Ashley ---
        if (g_net.initialized) {
            RE4MPPacket recvPkt;
            while (RecvPacket(&g_net, &recvPkt)) {
                switch (recvPkt.type) {
                case PKT_POSITION:
                    playerTwoPos[0] = recvPkt.pos[0];
                    playerTwoPos[1] = recvPkt.pos[1];
                    playerTwoPos[2] = recvPkt.pos[2];
                    playerTwoAng[0] = recvPkt.ang[0];
                    playerTwoAng[1] = recvPkt.ang[1];
                    playerTwoAng[2] = recvPkt.ang[2];
                    // Write position + rotation to Leon clone entity
                    if (playerTwoReady && playerTwoPtr) {
                        __try {
                            float* clonePos = GetCEmPos(playerTwoPtr);
                            float* cloneAng = GetCEmAng(playerTwoPtr);
                            clonePos[0] = recvPkt.pos[0];
                            clonePos[1] = recvPkt.pos[1];
                            clonePos[2] = recvPkt.pos[2];
                            cloneAng[0] = recvPkt.ang[0];
                            cloneAng[1] = recvPkt.ang[1];
                            cloneAng[2] = recvPkt.ang[2];
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            playerTwoReady = false;
                            playerTwoPtr = nullptr;
                        }
                    }
                    break;

                }
            }
        }

        // Exit
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            DbgLog("[RE4MP] END pressed — unloading\n");
            break;
        }

        Sleep(10);
    }

    DbgLog("[RE4MP] Cleaning up...\n");
    CleanupNetwork(&g_net);
    if (g_logFile) { fclose(g_logFile); g_logFile = NULL; }
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE) param, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID l)
{
        switch (reason)
        {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            CreateThread(NULL, NULL, MainThread, hModule, NULL, NULL);
            break;
        case DLL_PROCESS_DETACH:
        default:
            break;
        }

    return TRUE;
}

