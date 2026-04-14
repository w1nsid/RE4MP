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
    g_isHost = g_config.isHost;
    DbgLog("[RE4MP] Config loaded: ServerIP=%s, ServerPort=%d, HostMode=%s\n",
           g_config.serverIP, g_config.serverPort, g_isHost ? "HOST" : "CLIENT");

    // Initialize networking
    if (!InitNetwork(&g_net, &g_config)) {
        DbgLog("[RE4MP] ERROR: Failed to initialize network\n");
    }
    else {
        DbgLog("[RE4MP] Network initialized, connected to %s:%d\n", g_config.serverIP, g_config.serverPort);
    }

    DbgLog("[RE4MP] Entering main loop\n");
    bool f5Held = false;

    while (true) {

        // Force Ashley to be present in every area (sets STA_SUB_ASHLEY flag each frame)
        ForceAshleyPresent();

        // Auto-acquire Ashley pointer if she became available after a room transition
        if (playerTwoReady && !SubCharPointer()) {
            DbgLog("[RE4MP] Ashley lost (room transition?) — waiting for reload\n");
            playerTwoReady = false;
            playerTwoPtr = nullptr;
        }
        if (!playerTwoReady) {
            int* ashley = SubCharPointer();
            if (ashley) {
                playerTwoPtr = ashley;
                if (!g_detoursAttached) {
                    AttachDetours();
                }
                playerTwoReady = true;
                DbgLog("[RE4MP] Ashley auto-acquired at 0x%p — partner ready!\n", ashley);
            }
        }

        // F5: Activate partner (use existing Ashley entity — no createBack needed)
        if (GetAsyncKeyState(VK_F5) & 0x8000) {
            if (!f5Held) {
                f5Held = true;
                if (playerTwoReady) {
                    DbgLog("[RE4MP] F5 pressed — partner already active\n");
                } else {
                    DbgLog("[RE4MP] F5 pressed — activating partner\n");
                    int* ashley = SubCharPointer();
                    DbgLog("[RE4MP]   Ashley (SubChar) pointer: 0x%p\n", ashley);
                    if (ashley) {
                        playerTwoPtr = ashley;
                        DbgLog("[RE4MP]   Using Ashley entity at 0x%p\n", playerTwoPtr);
                        __try {
                            float* pos = GetCEmPos(playerTwoPtr);
                            DbgLog("[RE4MP]   Entity pos: %.1f, %.1f, %.1f\n", pos[0], pos[1], pos[2]);
                            DbgLog("[RE4MP]   Attaching detour hooks...\n");
                            AttachDetours();
                            DbgLog("[RE4MP]   Detours attached\n");
                            playerTwoReady = true;
                            DbgLog("[RE4MP]   Partner ready!\n");
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            DbgLog("[RE4MP]   CRASH reading Ashley entity! code=0x%08X\n", GetExceptionCode());
                            playerTwoPtr = nullptr;
                        }
                    } else {
                        DbgLog("[RE4MP]   ERROR: Ashley/SubChar not present in this area\n");
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
                float* myRot = GetCEmRot(playerPtr);
                if (myPos && myRot) {
                    RE4MPPacket sendPkt;
                    memset(&sendPkt, 0, sizeof(sendPkt));
                    sendPkt.type = PKT_POSITION;
                    sendPkt.entityType = ENT_PLAYER;
                    sendPkt.entityIndex = 0;
                    sendPkt.pos[0] = myPos[0]; sendPkt.pos[1] = myPos[1]; sendPkt.pos[2] = myPos[2];
                    sendPkt.rot[0] = myRot[0]; sendPkt.rot[1] = myRot[1]; sendPkt.rot[2] = myRot[2];
                    sendPkt.hp = -1;
                    SendPacket(&g_net, &sendPkt);
                }
            }

            // Send Ashley's position + rotation to remote player
            int* ashley = SubCharPointer();
            if (ashley) {
                __try {
                    float* ashPos = GetCEmPos(ashley);
                    float* ashRot = GetCEmRot(ashley);
                    RE4MPPacket ashPkt;
                    memset(&ashPkt, 0, sizeof(ashPkt));
                    ashPkt.type = PKT_ENTITY_STATE;
                    ashPkt.entityType = ENT_PARTNER;
                    ashPkt.entityIndex = 0;
                    ashPkt.pos[0] = ashPos[0]; ashPkt.pos[1] = ashPos[1]; ashPkt.pos[2] = ashPos[2];
                    ashPkt.rot[0] = ashRot[0]; ashPkt.rot[1] = ashRot[1]; ashPkt.rot[2] = ashRot[2];
                    ashPkt.hp = -1;
                    SendPacket(&g_net, &ashPkt);
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Send enemy + boss entity states
            // Throttle: only send every 3rd frame to reduce bandwidth
            static int entitySendFrame = 0;
            entitySendFrame++;
            if (g_isHost) {
                uint32_t emCount = GetEmCount();
                for (uint32_t i = 0; i < emCount; i++) {
                    int* em = GetEmByIndex(i);
                    if (!em || !IsCEmValid(em)) continue;

                    uint8_t id = GetCEmId(em);
                    bool isBoss = IsBossId(id);
                    bool isEnemy = IsEnemyId(id);

                    if (!isEnemy && !isBoss) continue;

                    // Bosses send every frame, enemies every 3rd frame
                    if (!isBoss && (entitySendFrame % 3) != 0) continue;

                    __try {
                        float* emPos = GetCEmPos(em);
                        float* emRot = GetCEmRot(em);
                        RE4MPPacket emPkt;
                        memset(&emPkt, 0, sizeof(emPkt));
                        emPkt.type = PKT_ENTITY_STATE;
                        emPkt.entityType = isBoss ? ENT_BOSS : ENT_ENEMY;
                        emPkt.entityIndex = (uint16_t)i;
                        emPkt.pos[0] = emPos[0]; emPkt.pos[1] = emPos[1]; emPkt.pos[2] = emPos[2];
                        emPkt.rot[0] = emRot[0]; emPkt.rot[1] = emRot[1]; emPkt.rot[2] = emRot[2];
                        emPkt.hp = GetCEmHP(em);
                        SendPacket(&g_net, &emPkt);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }

        // --- Network recv: apply remote entity states ---
        if (g_net.initialized) {
            RE4MPPacket recvPkt;
            while (RecvPacket(&g_net, &recvPkt)) {
                switch (recvPkt.type) {
                case PKT_POSITION:
                    // Remote player's Leon position → write to our Ashley (partner)
                    playerTwoPos[0] = recvPkt.pos[0];
                    playerTwoPos[1] = recvPkt.pos[1];
                    playerTwoPos[2] = recvPkt.pos[2];
                    if (playerTwoReady && playerTwoPtr) {
                        __try {
                            float* ashleyPos = GetCEmPos(playerTwoPtr);
                            ashleyPos[0] = recvPkt.pos[0];
                            ashleyPos[1] = recvPkt.pos[1];
                            ashleyPos[2] = recvPkt.pos[2];
                            float* ashleyRot = GetCEmRot(playerTwoPtr);
                            ashleyRot[0] = recvPkt.rot[0];
                            ashleyRot[1] = recvPkt.rot[1];
                            ashleyRot[2] = recvPkt.rot[2];
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            playerTwoReady = false;
                            playerTwoPtr = nullptr;
                        }
                    }
                    break;

                case PKT_ENTITY_STATE:
                    if (recvPkt.entityType == ENT_PARTNER) {
                        // Remote player's Ashley position — store for future use
                        remoteAshleyPos[0] = recvPkt.pos[0];
                        remoteAshleyPos[1] = recvPkt.pos[1];
                        remoteAshleyPos[2] = recvPkt.pos[2];
                    }
                    else if ((recvPkt.entityType == ENT_ENEMY || recvPkt.entityType == ENT_BOSS) && !g_isHost) {
                        // Apply enemy/boss state from host
                        uint32_t idx = recvPkt.entityIndex;
                        if (idx < GetEmCount()) {
                            int* em = GetEmByIndex(idx);
                            if (em && IsCEmValid(em)) {
                                __try {
                                    float* emPos = GetCEmPos(em);
                                    float* emRot = GetCEmRot(em);
                                    emPos[0] = recvPkt.pos[0]; emPos[1] = recvPkt.pos[1]; emPos[2] = recvPkt.pos[2];
                                    emRot[0] = recvPkt.rot[0]; emRot[1] = recvPkt.rot[1]; emRot[2] = recvPkt.rot[2];
                                    if (recvPkt.hp >= 0) {
                                        *(int16_t*)((DWORD)em + 0x324) = recvPkt.hp;
                                    }
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }
                        }
                    }
                    break;

                case PKT_ENTITY_DEATH:
                    // Remote notified an entity was killed
                    if (!g_isHost && recvPkt.entityIndex < GetEmCount()) {
                        int* em = GetEmByIndex(recvPkt.entityIndex);
                        if (em && IsCEmValid(em)) {
                            __try {
                                *(int16_t*)((DWORD)em + 0x324) = 0; // set HP to 0
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
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

